// dpdk_mirp_v10.cpp —— P7 定案版：收包预取 + 自适应(逐包/批量) free 与入队
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <atomic>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_cycles.h>
#include <rte_launch.h>
#include <rte_prefetch.h>
#include <rte_version.h>
#include "mirp.h"
#include "market2.h"

#define POOL_N 16384
#define RING   1024
#define BURST  32
#define QN     65536
#define MAXLAT 4000000

using namespace smdp;
static struct rte_mempool *g_mp;

template<class T,size_t N>
struct SPSC {
    static_assert((N & (N-1))==0,"N 必须 2 的幂");
    alignas(64) std::atomic<size_t> head{0};
    alignas(64) std::atomic<size_t> tail{0};
    alignas(64) T buf[N];
    bool push(const T& v){
        size_t h=head.load(std::memory_order_relaxed);
        size_t t=tail.load(std::memory_order_acquire);
        if(((h-t)&(N-1))==N-1) return false;
        buf[h]=v;
        head.store((h+1)&(N-1),std::memory_order_release);
        return true;
    }
    size_t push_bulk(const T* v,size_t n){          // 一次 head 更新
        size_t h=head.load(std::memory_order_relaxed);
        size_t t=tail.load(std::memory_order_acquire);
        size_t space=(N-1)-((h-t)&(N-1));
        if(n>space) n=space;
        for(size_t i=0;i<n;i++) buf[(h+i)&(N-1)]=v[i];
        if(n) head.store((h+n)&(N-1),std::memory_order_release);
        return n;
    }
    bool pop(T& v){
        size_t t=tail.load(std::memory_order_relaxed);
        if(t==head.load(std::memory_order_acquire)) return false;
        v=buf[t];
        tail.store((t+1)&(N-1),std::memory_order_release);
        return true;
    }
};

struct Tick { int64_t ino; double bid, ask; uint64_t t_prod; };

static SPSC<Tick,QN> g_q;
static uint64_t* g_lat=nullptr; static size_t g_nlat=0;
static std::atomic<bool> g_stop{false}, g_record{true};
static uint64_t g_consumed=0, g_dropped=0;

static int consumer_main(void*){
    Tick t;
    while(!g_stop.load(std::memory_order_relaxed)){
        while(g_q.pop(t)){
            if(g_record.load(std::memory_order_relaxed) && g_nlat<MAXLAT)
                g_lat[g_nlat++]=rte_rdtsc_precise()-t.t_prod;
            g_consumed++;
        }
    }
    while(g_q.pop(t)) g_consumed++;
    return 0;
}

static int port_init(uint16_t port){
    struct rte_eth_conf cfg; memset(&cfg,0,sizeof(cfg));
    int ret=rte_eth_dev_configure(port,1,1,&cfg); if(ret<0){printf("configure %d\n",ret);return ret;}
    ret=rte_eth_rx_queue_setup(port,0,RING,rte_eth_dev_socket_id(port),NULL,g_mp); if(ret<0){printf("rxq %d\n",ret);return ret;}
    ret=rte_eth_tx_queue_setup(port,0,RING,rte_eth_dev_socket_id(port),NULL); if(ret<0){printf("txq %d\n",ret);return ret;}
    ret=rte_eth_dev_start(port); if(ret<0){printf("start %d\n",ret);return ret;}
    rte_eth_promiscuous_enable(port);
    return 0;
}

static inline bool ValidMirp(const uint8_t* m,uint32_t mlen,MirpHeader& h){
    if(mlen<24+4) return false;
    if(ParseMirpHeader(m,mlen,h)!=0) return false;
    if(h.TypeID!=0x01 || (h.Flag&0x0F)!=1) return false;
    if(h.TopicID<1000 || h.TopicID>9999) return false;
    if((uint32_t)h.Length+24!=mlen) return false;
    return true;
}

static double pctp(uint64_t* v,size_t n,double p,double k){
    if(!n) return 0.0;
    size_t i=(size_t)((double)n*p/100.0); if(i>=n) i=n-1;
    return (double)v[i]*k;
}

static void fill_sample(uint8_t* buf){
    const char* hex="0101100002000000E903000002000000778B0D4FB42D000003000200000401100600313002000200";
    for(int i=0;i<40;i++){ unsigned v; sscanf(hex+2*i,"%2x",&v); buf[i]=(uint8_t)v; }
}

// SYNTH：进程内合成负载，测流水线吞吐（不经网卡）
static void run_synth(MarketVisitor& mv,double k){
    const int M=2000000;
    static uint8_t buf[64]; fill_sample(buf);
    MirpHeader h; Tick t{}; t.ino=0; t.bid=t.ask=0; t.t_prod=0;
    g_record.store(false);

    {   // A) 单条 push
        size_t done=0; int spin=0;
        uint64_t a=rte_rdtsc_precise();
        for(int i=0;i<M;i++){
            if(ValidMirp(buf,40,h)) IterateDomainsT(buf+24,h.Length,mv);
            t.t_prod=rte_rdtsc_precise();
            spin=0; while(!g_q.push(t)){ if(++spin>100000000) break; }
            done++;
        }
        uint64_t b=rte_rdtsc_precise();
        double ns=(double)(b-a)/done*k, mpps=done*1000.0/((b-a)*k);
        printf("SYNTH 单条 push : %.1f ns/msg, %.2f Mpps\n",ns,mpps);
    }
    {   // B) 批量 push（每 32 条一次 head）
        Tick batch[32];
        for(int i=0;i<32;i++) batch[i]=t;
        size_t done=0; int spin=0;
        uint64_t a=rte_rdtsc_precise();
        for(int i=0;i<M/32;i++){
            for(int j=0;j<32;j++){ if(ValidMirp(buf,40,h)) IterateDomainsT(buf+24,h.Length,mv); }
            size_t off=0; spin=0;
            while(off<32){ size_t put=g_q.push_bulk(batch,32); off+=put; if(!put && ++spin>100000000) break; }
            done+=32;
        }
        uint64_t b=rte_rdtsc_precise();
        double ns=(double)(b-a)/done*k, mpps=done*1000.0/((b-a)*k);
        printf("SYNTH 批量 push : %.1f ns/msg, %.2f Mpps（每 32 条一次 head 更新）\n",ns,mpps);
    }
    g_record.store(true);
}

int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    int eal=rte_eal_init(argc,argv);
    if(eal<0) return -1;
    int need=20000;
    for(int i=eal;i<argc;i++){ if(argv[i][0]>='0'&&argv[i][0]<='9'){need=atoi(argv[i]);break;} }
    if(need<=0) need=20000;

    g_mp=rte_pktmbuf_pool_create("p",POOL_N,256,0,RTE_MBUF_DEFAULT_BUF_SIZE,rte_socket_id());
    if(!g_mp){printf("mempool fail\n");return -1;}
    if(port_init(0)!=0) return -1;
    rte_eth_dev_set_link_up(0); rte_delay_ms(2000);

    MarketStore store; MarketVisitor mv(store);
    for(int i=0;i<1000;i++){auto&s=store.Get(i);s.info.CodecPrice=18000.0;s.info.PriceTick=5.0;}
    g_lat=(uint64_t*)malloc(sizeof(uint64_t)*MAXLAT);

    uint64_t hz=rte_get_tsc_hz(); double k=1e9/(double)hz;
    uint64_t cal=0; const int CAL_N=200000;
    for(int i=0;i<CAL_N;i++){uint64_t a=rte_rdtsc_precise();uint64_t b=rte_rdtsc_precise();cal+=(b-a);}
    double ov=(double)cal/CAL_N*k;

    unsigned lc_main=rte_lcore_id();
    unsigned lc_cons=rte_get_next_lcore(lc_main,1,0);
    printf("main lcore=%u consumer lcore=%u；rdtsc 对开销=%.2f ns\n",lc_main,lc_cons,ov);
    if(rte_eal_remote_launch(consumer_main,nullptr,lc_cons)!=0){printf("launch fail\n");return -1;}
    rte_delay_ms(200);

    if(getenv("SYNTH")){
        printf("=== SYNTH 合成流水线（不经网卡）===\n");
        run_synth(mv,k);
        g_stop.store(true); rte_eal_wait_lcore(lc_cons);
        printf("消费 %llu 条\n",(unsigned long long)g_consumed);
        return 0;
    }

    std::vector<uint64_t> seg; seg.reserve(need/8+32);
    Tick batch[BURST]; size_t bn=0;
    uint64_t pkts=0, total_enq=0; int64_t topic=-1;
    const uint16_t kHdr=14+20+8;
    double t0=(double)rte_rdtsc_precise()/hz, t_end=t0+120.0;
    printf("等待 MIRP 流量...\n");

    for(;;){
        struct rte_mbuf*mb[BURST];
        uint16_t nb=rte_eth_rx_burst(0,0,mb,BURST);
        { static double tlast=0; double nw=(double)rte_rdtsc_precise()/hz;
          if(nw-tlast>=1.0){ struct rte_eth_stats st; rte_eth_stats_get(0,&st);
            printf("[mon] t=%.0fs 解出=%llu 入队=%llu 丢弃=%llu\n",nw-t0,
                   (unsigned long long)pkts,(unsigned long long)total_enq,(unsigned long long)g_dropped);
            tlast=nw; } }
        if(nb==0){ if((double)rte_rdtsc_precise()/hz>t_end) break; continue; }

        uint64_t t_rx=rte_rdtsc_precise();
        const bool big = (nb >= 8);                       // 突发才走批量
        for(uint16_t i=0;i<nb;i++){
            if(i+1<nb) rte_prefetch0(rte_pktmbuf_mtod(mb[i+1],void*));
            uint8_t*d=rte_pktmbuf_mtod(mb[i],uint8_t*); uint16_t len=mb[i]->data_len;
            if(len>kHdr){
                const uint8_t* mirp=d+kHdr; uint16_t mlen=len-kHdr;
                MirpHeader h;
                if(ValidMirp(mirp,mlen,h)){
                    if(topic<0){ topic=h.TopicID; printf("[first] topic=%d Length=%u\n",(int)h.TopicID,(unsigned)h.Length); }
                    IterateDomainsT(mirp+24,h.Length,mv);
                    ++pkts;
                    for(int64_t ino : mv.Touched()){
                        auto& st=store.Get(ino);
                        Tick t; t.ino=ino; t.bid=st.book.Bid[0].Price; t.ask=st.book.Ask[0].Price; t.t_prod=rte_rdtsc_precise();
                        if(big){
                            batch[bn++]=t;
                            if(bn==BURST){ size_t put=g_q.push_bulk(batch,BURST); total_enq+=put; if(put<BURST) g_dropped+=(BURST-put); bn=0; }
                        } else { if(g_q.push(t)) total_enq++; else g_dropped++; }
                    }
                    mv.ClearTouched();
                }
            }
            if(!big) rte_pktmbuf_free(mb[i]);
        }
        if(big){
#if defined(RTE_VERSION) && RTE_VERSION >= RTE_VERSION_NUM(19,11,0,0)
            rte_pktmbuf_free_bulk(mb,nb);
#else
            for(uint16_t i=0;i<nb;i++) rte_pktmbuf_free(mb[i]);
#endif
            size_t off=0; int spin=0;
            while(off<bn){
                size_t put=g_q.push_bulk(batch+off,bn-off); off+=put;
                if(!put && ++spin>200000){ g_dropped+=(bn-off); break; }
            }
            total_enq+=off; bn=0;
        }
        uint64_t t_done=rte_rdtsc_precise();
        seg.push_back((t_done-t_rx)/nb);
        if(pkts>=(uint64_t)need) break;
    }

    g_stop.store(true);
    rte_eal_wait_lcore(lc_cons);

    printf("\n=== 结果 ===\n");
    printf("解出 %llu, 入队 %llu, 丢弃 %llu, 消费 %llu, topic=%lld\n",
           (unsigned long long)pkts,(unsigned long long)total_enq,
           (unsigned long long)g_dropped,(unsigned long long)g_consumed,(long long)topic);
    if(!seg.empty()){
        std::sort(seg.begin(),seg.end());
        printf("口径B 处理段(自适应,不含收包): p50=%.1f p99=%.1f ns/包\n",
               pctp(seg.data(),seg.size(),50,k), pctp(seg.data(),seg.size(),99,k));
    }
    if(g_nlat){
        std::sort(g_lat,g_lat+g_nlat);
        printf("跨核端到端: p50=%.1f p99=%.1f p999=%.1f ns（已扣 rdtsc %.1f）\n",
               pctp(g_lat,g_nlat,50,k)-ov, pctp(g_lat,g_nlat,99,k)-ov, pctp(g_lat,g_nlat,999,k)-ov, ov);
        printf("延迟样本 %zu\n",g_nlat);
    }
    return 0;
}

