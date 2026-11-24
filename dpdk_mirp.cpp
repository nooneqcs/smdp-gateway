#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#define MAX_S  2000000

using namespace smdp;
static struct rte_mempool *g_mp;
static uint64_t g_samp[MAX_S];

static int cmp_u64(const void*a,const void*b){uint64_t x=*(const uint64_t*)a,y=*(const uint64_t*)b;return (x>y)-(x<y);}

static int port_init(uint16_t port){
    struct rte_eth_conf cfg; memset(&cfg,0,sizeof(cfg));
    int ret=rte_eth_dev_configure(port,1,1,&cfg); if(ret<0){printf("configure %d\n",ret);return ret;}
    ret=rte_eth_rx_queue_setup(port,0,RING,rte_eth_dev_socket_id(port),NULL,g_mp); if(ret<0){printf("rxq %d\n",ret);return ret;}
    ret=rte_eth_tx_queue_setup(port,0,RING,rte_eth_dev_socket_id(port),NULL); if(ret<0){printf("txq %d\n",ret);return ret;}
    ret=rte_eth_dev_start(port); if(ret<0){printf("start %d\n",ret);return ret;}
    rte_eth_promiscuous_enable(port);   // 收所有帧（笔记本发的 dst MAC 不是本机也要收）
    return 0;
}

int main(int argc,char**argv){
    setvbuf(stdout,NULL,_IONBF,0);
    if(rte_eal_init(argc,argv)<0) return -1;
    g_mp=rte_pktmbuf_pool_create("p",POOL_N,256,0,RTE_MBUF_DEFAULT_BUF_SIZE,rte_socket_id());
    if(!g_mp){printf("mempool fail\n");return -1;}
    if(port_init(0)!=0) return -1;
    rte_eth_dev_set_link_up(0); rte_delay_ms(2000);

    MarketStore store; MarketVisitor mv(store);
    for(int i=0;i<1000;i++){auto&s=store.Get(i);s.info.CodecPrice=18000.0;s.info.PriceTick=5.0;} // 预置，真实网关由快照 0x0101 填

    uint64_t hz=rte_get_tsc_hz(); double k=1e9/(double)hz;
    int n=0; uint64_t pkts=0; int64_t topic=-1;
    const uint16_t kHdr=14+20+8;   // eth+ipv4+udp（VLAN 就改 46）
    printf("等待 MIRP 流量...\n");

    double t0=(double)rte_rdtsc_precise()/hz, t_end=t0+120.0;
    while(n<MAX_S){
        struct rte_mbuf*mb[BURST];
        uint16_t nb=rte_eth_rx_burst(0,0,mb,BURST);
        if(nb==0){ if((double)rte_rdtsc_precise()/hz>t_end) break; continue; }
        uint64_t t_rx=rte_rdtsc_precise();
        for(uint16_t i=0;i<nb;i++){
            uint8_t*d=rte_pktmbuf_mtod(mb[i],uint8_t*); uint16_t len=mb[i]->data_len;
            if(len>kHdr){
                const uint8_t* mirp=d+kHdr; uint16_t mlen=len-kHdr;
                MirpHeader h;
                if(ParseMirpHeader(mirp,mlen,h)==0){
                    if(topic<0) topic=h.TopicID;
                    IterateDomainsT(mirp+24,h.Length,mv);
                    ++pkts;
                }
            }
            rte_pktmbuf_free(mb[i]);
        }
        uint64_t t_done=rte_rdtsc_precise();
        g_samp[n++]=(t_done-t_rx)/nb;   // 处理段（不含收包）/包
    }

    printf("样本 %d, 解出 MIRP 包 %llu, topic=%lld\n",n,(unsigned long long)pkts,(long long)topic);
    if(n==0){printf("没收到/没解出包\n");return 0;}
    qsort(g_samp,n,sizeof(uint64_t),cmp_u64);
    double ov=25.9; // rdtsc_precise 单次开销（两次/ burst，已近似）
    #define Q(p) (g_samp[((long)n*(p)/100>=n)?n-1:(int)((long)n*(p)/100)]*k)
    printf("处理段 ns/包: p50=%.1f p99=%.1f (未扣 rdtsc ~%.0fns; burst 摊薄)\n",Q(50)-ov,Q(99)-ov,ov);
    #undef Q
    // 打印一个合约的 bid1（确认 book 被更新）
    for(int i=0;i<1000;i++){ if(store.Get(i).book.Bid[0].Volume){ printf("contract %d Bid1=%.1f/%lld\n",i,store.Get(i).book.Bid[0].Price,(long long)store.Get(i).book.Bid[0].Volume); break; } }
    return 0;
}

