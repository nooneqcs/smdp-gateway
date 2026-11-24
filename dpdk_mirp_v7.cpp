// dpdk_mirp_v7.cpp —— P7 联调 + 延迟打点（纯解码 / 处理段 双口径）
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_cycles.h>
#include "mirp.h"
#include "market2.h"

#define POOL_N 16384
#define RING   1024
#define BURST  32
#define MAX_TARGET 5000000

using namespace smdp;
static struct rte_mempool *g_mp;

static int port_init(uint16_t port){
    struct rte_eth_conf cfg; memset(&cfg,0,sizeof(cfg));
    int ret=rte_eth_dev_configure(port,1,1,&cfg); if(ret<0){printf("configure %d\n",ret);return ret;}
    ret=rte_eth_rx_queue_setup(port,0,RING,rte_eth_dev_socket_id(port),NULL,g_mp); if(ret<0){printf("rxq %d\n",ret);return ret;}
    ret=rte_eth_tx_queue_setup(port,0,RING,rte_eth_dev_socket_id(port),NULL); if(ret<0){printf("txq %d\n",ret);return ret;}
    ret=rte_eth_dev_start(port); if(ret<0){printf("start %d\n",ret);return ret;}
    rte_eth_promiscuous_enable(port);   // 保留：笔记本 dst MAC 不是本机
    return 0;
}

static inline bool ValidMirp(const uint8_t* m, uint32_t mlen, MirpHeader& h){
    if(mlen < 24+4) return false;
    if(ParseMirpHeader(m, mlen, h) != 0) return false;
    if(h.TypeID != 0x01 || (h.Flag & 0x0F) != 1) return false;
    if(h.TopicID < 1000 || h.TopicID > 9999) return false;
    if((uint32_t)h.Length + 24 != mlen) return false;
    return true;
}

static double pct(std::vector<uint64_t>& v,double p,double k){
    if(v.empty()) return 0.0;
    size_t idx=(size_t)((double)v.size()*p/100.0);
    if(idx>=v.size()) idx=v.size()-1;
    return (double)v[idx]*k;
}

int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    int eal=rte_eal_init(argc,argv);
    if(eal<0) return -1;

    int need=20000;                                   // 用法：... -a 0000:01:00.0 -- 20000
    for(int i=eal;i<argc;i++){ if(argv[i][0]>='0'&&argv[i][0]<='9'){ need=atoi(argv[i]); break; } }
    if(need<=0) need=20000; if(need>MAX_TARGET) need=MAX_TARGET;

    g_mp=rte_pktmbuf_pool_create("p",POOL_N,256,0,RTE_MBUF_DEFAULT_BUF_SIZE,rte_socket_id());
    if(!g_mp){printf("mempool fail\n");return -1;}
    if(port_init(0)!=0) return -1;
    rte_eth_dev_set_link_up(0); rte_delay_ms(2000);

    MarketStore store; MarketVisitor mv(store);
    for(int i=0;i<1000;i++){auto&s=store.Get(i);s.info.CodecPrice=18000.0;s.info.PriceTick=5.0;}

    uint64_t hz=rte_get_tsc_hz(); double k=1e9/(double)hz;

    // rdtsc 成对开销：本机实测，替代硬编码 25.9
    uint64_t cal=0; const int CAL_N=200000;
    for(int i=0;i<CAL_N;i++){uint64_t a=rte_rdtsc_precise();uint64_t b=rte_rdtsc_precise();cal+=(b-a);}
    double ov=(double)cal/CAL_N*k;
    printf("目标解出 %d 条；rdtsc 成对开销 = %.2f ns（本机实测 %d 次）\n",need,ov,CAL_N);

    std::vector<uint64_t> dec; dec.reserve(need);        // 纯解码：每条一个 tick
    std::vector<uint64_t> seg; seg.reserve(need/8+32);   // 处理段：每 burst 一个

    uint64_t pkts=0, rx_total=0; int64_t topic=-1;
    const uint16_t kHdr=14+20+8;                         // eth+ipv4+udp（VLAN 改 46）
    const char* dump=getenv("DUMP"); int dumped=0;
    printf("等待 MIRP 流量...\n");
    double t0=(double)rte_rdtsc_precise()/hz, t_end=t0+120.0;

    for(;;){
        struct rte_mbuf*mb[BURST];
        uint16_t nb=rte_eth_rx_burst(0,0,mb,BURST);
        rx_total+=nb;
        { static double tlast=0; double nw=(double)rte_rdtsc_precise()/hz;
          if(nw-tlast>=1.0){ struct rte_eth_stats st; rte_eth_stats_get(0,&st);
            printf("[mon] t=%.0fs rx=%llu 解出=%llu ipackets=%llu imissed=%llu\n",
                   nw-t0,(unsigned long long)rx_total,(unsigned long long)pkts,
                   (unsigned long long)st.ipackets,(unsigned long long)st.imissed);
            tlast=nw; } }
        if(nb==0){ if((double)rte_rdtsc_precise()/hz>t_end) break; continue; }

        uint64_t t_rx=rte_rdtsc_precise();
        for(uint16_t i=0;i<nb;i++){
            uint8_t*d=rte_pktmbuf_mtod(mb[i],uint8_t*); uint16_t len=mb[i]->data_len;
            if(dump && dumped<5 && len>=14 && d[12]==0x08 && d[13]==0x00){
                dumped++; printf("[pkt len=%u] ",len);
                for(int b=0;b<32&&b<len;b++) printf("%02x",d[b]); printf("\n");
            }
            if(len>kHdr){
                const uint8_t* mirp=d+kHdr; uint16_t mlen=len-kHdr;
                uint64_t tp0=rte_rdtsc_precise();
                MirpHeader h; bool ok=ValidMirp(mirp,mlen,h);
                if(ok){
                    if(topic<0){ topic=h.TopicID; printf("[first] topic=%d Length=%u mlen=%u\n",(int)h.TopicID,(unsigned)h.Length,(unsigned)mlen); }
                    IterateDomainsT(mirp+24,h.Length,mv);
                    ++pkts;
                }
                uint64_t tp1=rte_rdtsc_precise();
                if(ok && dec.size()<(size_t)MAX_TARGET) dec.push_back(tp1-tp0);
            }
            rte_pktmbuf_free(mb[i]);
        }
        uint64_t t_done=rte_rdtsc_precise();
        seg.push_back((t_done-t_rx)/nb);
        if(pkts>=(uint64_t)need) break;
    }

    printf("\n=== 结果 ===\n");
    printf("解出 MIRP 包 %llu 条, topic=%lld, rx_total=%llu\n",
           (unsigned long long)pkts,(long long)topic,(unsigned long long)rx_total);
    if(dec.empty()){ printf("没解出包，无法统计\n"); return 0; }

    std::sort(dec.begin(),dec.end());
    std::sort(seg.begin(),seg.end());
    printf("口径A 纯解码(校验+域解码+book合并，单条): p50=%.1f p90=%.1f p99=%.1f p999=%.1f ns（已扣 rdtsc %.1f）\n",
           pct(dec,50,k)-ov, pct(dec,90,k)-ov, pct(dec,99,k)-ov, pct(dec,99.9,k)-ov, ov);
    printf("口径B 处理段(burst 处理完/nb，不含收包): p50=%.1f p99=%.1f ns/包（含 rdtsc 摊销 ~%.1f/32，未扣）\n",
           pct(seg,50,k), pct(seg,99,k), ov);
    printf("样本: dec=%zu 条, seg=%zu 个 burst\n", dec.size(), seg.size());

    for(int i=0;i<1000;i++){
        if(store.Get(i).book.Bid[0].Volume){
            printf("首个有量合约 %d: Bid1=%.4f vol=%lld\n", i,
                   store.Get(i).book.Bid[0].Price,(long long)store.Get(i).book.Bid[0].Volume);
            break;
        }
    }
    return 0;
}
