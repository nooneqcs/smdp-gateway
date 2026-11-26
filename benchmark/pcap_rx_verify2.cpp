// pcap_rx_verify2.cpp —— net_pcap 收 → 解码 → 与 ticks_simnow.bin 全帧比对合约头 + 吞吐
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_cycles.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <utility>
#include "mirp.h"
#include "decode.h"
#include "delta.h"
#include "wire_frame.h"

#pragma pack(push,1)
struct RecTick { char instrument[16]; int64_t exchangeTsUs; double last,bid1,ask1,preSettle;
                 int64_t bid1Vol,ask1Vol,volume; double turnover,openInterest; };
#pragma pack(pop)

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
    // 1) 读 ticks，重建每帧的期望 (InstrumentNo, ChangeNo)
    std::vector<std::pair<int64_t,int64_t>> exp;
    {
        FILE* f=fopen("ticks_simnow.bin","rb");
        if(!f){ printf("open ticks_simnow.bin fail (需在 benchmark/ 下运行)\n"); return 1; }
        std::map<std::string,int64_t> ino, cn; RecTick t;
        while(fread(&t,sizeof t,1,f)==1){
            std::string k(t.instrument);
            if(!ino.count(k)){ ino[k]=(int64_t)ino.size()+1; cn[k]=0; }
            exp.push_back({ino[k], ++cn[k]});
        }
        fclose(f);
    }
    printf("ticks=%zu\n", exp.size());

    // 2) DPDK net_pcap 收
    if(rte_eal_init(argc,argv)<0) return 1;
    if(rte_eth_dev_count_avail()<1){ printf("no port\n"); return 1; }
    struct rte_eth_dev_info di; int rc=rte_eth_dev_info_get(0,&di); (void)rc;
    printf("driver=%s\n", di.driver_name);
    struct rte_eth_conf conf; __builtin_memset(&conf,0,sizeof conf);
    if(rte_eth_dev_configure(0,1,0,&conf)<0){ printf("configure fail\n"); return 1; }
    struct rte_mempool* mp=rte_pktmbuf_pool_create("mp",16384,0,0,2048,rte_socket_id());
    if(!mp){ printf("mempool fail\n"); return 1; }
    struct rte_eth_rxconf rxq; __builtin_memset(&rxq,0,sizeof rxq);
    if(rte_eth_rx_queue_setup(0,0,1024,rte_socket_id(),&rxq,mp)<0){ printf("rxq fail\n"); return 1; }
    if(rte_eth_dev_start(0)<0){ printf("start fail\n"); return 1; }
    rte_eth_promiscuous_enable(0);

    struct rte_mbuf* m[64];
    long frames=0, okframes=0, doms=0, headmismatch=0, idle=0;
    uint64_t hz=rte_get_tsc_hz(), t0=rte_rdtsc();
    for(long it=0; it<200000000; ++it){
        uint16_t n=rte_eth_rx_burst(0,0,m,64);
        if(!n){ if(++idle>5000000) break; continue; }
        idle=0;
        for(uint16_t j=0;j<n;++j){
            ++frames;
            wire::View v{};
            if(wire::ParseUdpFrame(rte_pktmbuf_mtod(m[j],const uint8_t*), rte_pktmbuf_pkt_len(m[j]), v)){
                Collected c; int r=smdp::IterateDomainsT(v.payload,v.len,c);
                if(r>=0){
                    ++okframes; doms+=c.n;
                    if(!exp.empty()){
                        auto& e=exp[(okframes-1)%exp.size()];
                        if(c.H.InstrumentNo!=e.first || c.H.ChangeNo!=e.second){
                            if(headmismatch<3)
                                printf("MISMATCH frame#%ld: got(ino=%lld,cn=%lld) expect(ino=%lld,cn=%lld)\n",
                                    okframes-1,(long long)c.H.InstrumentNo,(long long)c.H.ChangeNo,
                                    (long long)e.first,(long long)e.second);
                            ++headmismatch;
                        }
                    }
                }
            }
            rte_pktmbuf_free(m[j]);
        }
    }
    uint64_t t1=rte_rdtsc(); double sec=(double)(t1-t0)/hz;
    printf("\nRESULT: frames=%ld okframes=%ld doms=%ld head_mismatch=%ld\n",
           frames, okframes, doms, headmismatch);
    printf("time=%.3fs  throughput=%.2f Mframes/s\n", sec, frames/sec/1e6);
    return 0;
}
