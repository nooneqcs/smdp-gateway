// pcap_rx_verify.cpp —— net_pcap 虚拟口读 pcap，解帧+解码，独立验证 pcap->decode
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <cstdio>
#include <cstdint>
#include "mirp.h"
#include "decode.h"
#include "delta.h"
#include "wire_frame.h"

#define PORT 0
#define NRXD 512

struct Collected {
    smdp::ContractDeltaHead H{}; smdp::PriceLevelDelta PL{}; smdp::TradeSummary TS{}; int n=0;
    void OnDomain(const smdp::DomainRef& d){
        switch(d.FieldID){
            case 0x0003: smdp::DecodeContractHead(d,H); break;
            case 0x1001: smdp::DecodePriceLevelDelta(d,PL); break;
            case 0x1002: smdp::DecodeTradeSummary(d,TS); break;
        } ++n;
    }
};

int main(int argc,char**argv){
    if(rte_eal_init(argc,argv)<0) return 1;
    uint16_t np = rte_eth_dev_count_avail();
    printf("ports=%u\n", np);
    if(!np){ printf("NO PORT (net_pcap 没加载?)\n"); return 1; }
    struct rte_eth_dev_info di; rte_eth_dev_info_get(PORT,&di);
    printf("driver=%s\n", di.driver_name);

    struct rte_eth_conf conf; __builtin_memset(&conf,0,sizeof conf);
    if(rte_eth_dev_configure(PORT,1,0,&conf)<0){ printf("configure fail\n"); return 1; }
    struct rte_mempool* mp = rte_pktmbuf_pool_create("mp",8192,0,0,2048,rte_socket_id());
    if(!mp){ printf("mempool fail\n"); return 1; }
    struct rte_eth_rxconf rxq; __builtin_memset(&rxq,0,sizeof rxq);
    if(rte_eth_rx_queue_setup(PORT,0,NRXD,rte_socket_id(),&rxq,mp)<0){ printf("rxq fail\n"); return 1; }
    if(rte_eth_dev_start(PORT)<0){ printf("start fail\n"); return 1; }
    rte_eth_promiscuous_enable(PORT);

    struct rte_mbuf* m[64];
    long frames=0, okframes=0, doms=0, idle=0;
    for(long it=0; it<200000000; ++it){
        uint16_t n = rte_eth_rx_burst(PORT,0,m,64);
        if(!n){ if(++idle>5000000) break; continue; }
        idle=0;
        for(uint16_t j=0;j<n;++j){
            ++frames;
            wire::View v{};
            if(wire::ParseUdpFrame(rte_pktmbuf_mtod(m[j],const uint8_t*),
                                   rte_pktmbuf_pkt_len(m[j]), v)){
                Collected c; int r = smdp::IterateDomainsT(v.payload, v.len, c);
                if(r>=0){ ++okframes; doms += c.n;
                    if(okframes==1) printf("首包: domains=%d ino=%lld changeNo=%lld\n",
                        c.n,(long long)c.H.InstrumentNo,(long long)c.H.ChangeNo);
                }
            }
            rte_pktmbuf_free(m[j]);
        }
    }
    printf("\nRESULT: frames=%ld okframes=%ld doms=%ld\n", frames, okframes, doms);
    return 0;
}
