// dpdk_mirp_v8.cpp —— P7: 收包→解码→SPSC 跨核分发
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
        size_t nt=(h+1)&(N-1);
        if(nt==tail.load(std::memory_order_acquire)) return false;
        buf[h]=v;
        head.store(nt,std::memory_order_release);
        return true;
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
static std::atomic<bool> g_stop{false};
static uint64_t g_consumed=0, g_dropped=0;

static int consumer_main(void*){
    Tick t;
    while(!g_stop.load(std::memory_order_relaxed)){
        while(g_q.pop(t)){
            if(g_nlat<MAXLAT) g_lat[g_nlat++]=rte_rdtsc_precise()-t.t_prod;
            g_consumed++;
        }
    }
    while(g_q.pop(t)){
        if(g_nlat<MAXLAT) g_lat[g_nlat++]=rte_rdtsc_precise()-t.t_prod;
        g_consumed++;
    }
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

    unsigned lc_main=rte_lcore_id();
    unsigned lc_cons=rte_get_next_lcore(lc_main,1,0);
    printf("main lcore=%u, consumer lcore=%u（需要 -l 1,2 才跑得起来）\n",lc_main,lc_cons);
    if(rte_eal_remote_launch(consumer_main,nullptr,lc_cons)!=0){printf("launch fail\n");return -1;}

    uint64_t hz=rte_get_tsc_hz(); double k=1e9/(double)hz;
    uint64_t cal=0; const int CAL_N=200000;
    for(int i=0;i<CAL_N;i++){uint64_t a=rte_rdtsc_precise();uint64_t b=rte_rdtsc_precise();cal+=(b-a);}
    double ov=(double)cal/CAL_N*k;
    printf("rdtsc 成对开销 = %.2f ns\n",ov);

    std::vector<uint64_t> enq; enq.reserve(need);
    uint64_t pkts=0; int64_t topic=-1;
    const uint16_t kHdr=14+20+8;
    double t0=(double)rte_rdtsc_precise()/hz, t_end=t0+120.0;
    printf("等待 MIRP 流量...\n");

    for(;;){
        struct rte_mbuf*mb[BURST];
        uint16_t nb=rte_eth_rx_burst(0,0,mb,BURST);
        if(nb==0){ if((double)rte_rdtsc_precise()/hz>t_end) break; continue; }
        for(uint16_t i=0;i<nb;i++){
            uint8_t*d=rte_pktmbuf_mtod(mb[i],uint8_t*); uint16_t len=mb[i]->data_len;
            if(len>kHdr){
                const uint8_t* mirp=d+kHdr; uint16_t mlen=len-kHdr;
                MirpHeader h;
                if(ValidMirp(mirp,mlen,h)){
                    if(topic<0){ topic=h.TopicID; printf("[first] topic=%d Length=%u\n",(int)h.TopicID,(unsigned)h.Length); }
                    IterateDomainsT(mirp+24,h.Length,mv);
                    ++pkts;
                    int64_t ino=mv.LastIno();
                    auto& st=store.Get(ino);
                    Tick t; t.ino=ino; t.bid=st.book.Bid[0].Price; t.ask=st.book.Ask[0].Price; t.t_prod=rte_rdtsc_precise();
                    uint64_t e0=rte_rdtsc_precise();
                    bool ok=g_q.push(t);
                    uint64_t e1=rte_rdtsc_precise();
                    if(ok) enq.push_back(e1-e0); else g_dropped++;
                }
            }
            rte_pktmbuf_free(mb[i]);
        }
        if(pkts>=(uint64_t)need) break;
    }

    g_stop.store(true);
    rte_eal_wait_lcore(lc_cons);

    printf("\n=== 结果 ===\n");
    printf("解出 %llu, 入队 %zu, 丢弃 %llu, 消费 %llu, topic=%lld\n",
           (unsigned long long)pkts,enq.size(),(unsigned long long)g_dropped,
           (unsigned long long)g_consumed,(long long)topic);
    if(!enq.empty()){
        std::sort(enq.begin(),enq.end());
        printf("入队 SPSC: p50=%.1f p99=%.1f ns（已扣 rdtsc %.1f）\n",
               pctp(enq.data(),enq.size(),50,k)-ov, pctp(enq.data(),enq.size(),99,k)-ov, ov);
    }
    if(g_nlat){
        std::sort(g_lat,g_lat+g_nlat);
        printf("跨核端到端(t_prod→consumer pop): p50=%.1f p99=%.1f p999=%.1f ns（已扣 rdtsc %.1f）\n",
               pctp(g_lat,g_nlat,50,k)-ov, pctp(g_lat,g_nlat,99,k)-ov, pctp(g_lat,g_nlat,99.9,k)-ov, ov);
        printf("延迟样本 %zu 条\n",g_nlat);
    }
    return 0;
}

