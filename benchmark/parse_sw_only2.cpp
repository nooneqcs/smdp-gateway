// parse_sw_only2.cpp —— v2: 预剥帧 + empty 基线 + 帧/走域/解码三段拆解
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <vector>
#include <algorithm>
#include <x86intrin.h>
#include "ThostFtdcUserApiStruct.h"
#include "mirp.h"
#include "decode.h"
#include "delta.h"
#include "wire_frame.h"

static constexpr double TSC_GHZ = 3.0;
static constexpr int ROUNDS = 20000;

/* ===== CTP ===== */
#pragma pack(push,1)
struct RecTick { char instrument[16]; int64_t exchangeTsUs; double last,bid1,ask1,preSettle;
                 int64_t bid1Vol,ask1Vol,volume; double turnover,openInterest; };
#pragma pack(pop)
static_assert(sizeof(RecTick)==96,"");
struct OutCTP { char instrument[16]; double last,bid1,ask1,preSettle,turnover,openInterest;
                int64_t volume,bid1Vol,ask1Vol; double open,high,low,upper,lower,preClose;
                int32_t hhmmss,tradingDay; };
static_assert(sizeof(OutCTP)==144,"");
static constexpr int CTP_FIELDS=17, CTP_STORES=18;

static inline int32_t ph(const char* s){return (s[0]-'0')*100000+(s[1]-'0')*10000+(s[3]-'0')*1000
                                        +(s[4]-'0')*100+(s[6]-'0')*10+(s[7]-'0');}
static inline int32_t py(const char* s){return (s[0]-'0')*10000000+(s[1]-'0')*1000000+(s[2]-'0')*100000
                                        +(s[3]-'0')*10000+(s[4]-'0')*1000+(s[5]-'0')*100
                                        +(s[6]-'0')*10+(s[7]-'0');}
__attribute__((noinline)) static void ctp_decode(const CThostFtdcDepthMarketDataField* p, OutCTP& o){
    std::memcpy(o.instrument,p->InstrumentID,16);
    o.last=p->LastPrice;o.bid1=p->BidPrice1;o.ask1=p->AskPrice1;
    o.preSettle=p->PreSettlementPrice;o.turnover=p->Turnover;o.openInterest=p->OpenInterest;
    o.volume=p->Volume;o.bid1Vol=p->BidVolume1;o.ask1Vol=p->AskVolume1;
    o.open=p->OpenPrice;o.high=p->HighestPrice;o.low=p->LowestPrice;
    o.upper=p->UpperLimitPrice;o.lower=p->LowerLimitPrice;o.preClose=p->PreClosePrice;
    o.hhmmss=ph(p->UpdateTime);o.tradingDay=py(p->TradingDay);
}
__attribute__((noinline)) static void ctp_empty(const CThostFtdcDepthMarketDataField*, OutCTP& o){o.hhmmss=0;}

/* ===== MIRP ===== */
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
struct Noop { int n=0; void OnDomain(const smdp::DomainRef&){++n;} };

static std::vector<std::vector<uint8_t>> load_pcap(const char* path){
    std::vector<std::vector<uint8_t>> out;
    FILE* f=std::fopen(path,"rb"); if(!f){std::perror("pcap");return out;}
    uint8_t gh[24]; if(std::fread(gh,1,24,f)!=24){std::fclose(f);return out;}
    uint32_t magic; std::memcpy(&magic,gh,4);
    bool swap=(magic==0xd4c3b2a1u||magic==0x4d3cb2a1u);
    for(;;){
        uint8_t phb[16]; if(std::fread(phb,1,16,f)!=16) break;
        uint32_t raw; std::memcpy(&raw,phb+8,4);
        uint32_t incl=swap?__builtin_bswap32(raw):raw;
        if(incl>65535u) break;
        std::vector<uint8_t> b(incl);
        if(std::fread(b.data(),1,incl,f)!=incl) break;
        out.push_back(std::move(b));
    }
    std::fclose(f); return out;
}

int main(int argc,char**argv){
    const char* tp=argc>1?argv[1]:"ticks_simnow.bin";
    const char* pp=argc>2?argv[2]:"mirp_out.pcap";
    int cap=argc>3?std::atoi(argv[3]):256;
    volatile double sink=0;

    /* CTP */
    FILE* f=std::fopen(tp,"rb"); if(!f){std::perror("ticks");return 1;}
    std::fseek(f,0,SEEK_END); long sz=std::ftell(f); std::fseek(f,0,SEEK_SET);
    int n_rec=(int)(sz/sizeof(RecTick));
    std::vector<RecTick> recs(n_rec);
    if(std::fread(recs.data(),sizeof(RecTick),n_rec,f)!=(size_t)n_rec) return 1;
    std::fclose(f);
    int nc=std::min(n_rec,cap);
    std::vector<CThostFtdcDepthMarketDataField> v(nc);
    for(int i=0;i<nc;++i){
        auto& d=v[i]; std::memset(&d,0,sizeof(d));
        std::memcpy(d.InstrumentID,recs[i].instrument,16);
        d.LastPrice=recs[i].last;d.BidPrice1=recs[i].bid1;d.AskPrice1=recs[i].ask1;
        d.PreSettlementPrice=recs[i].preSettle;
        d.BidVolume1=(int)recs[i].bid1Vol;d.AskVolume1=(int)recs[i].ask1Vol;d.Volume=(int)recs[i].volume;
        d.Turnover=recs[i].turnover;d.OpenInterest=recs[i].openInterest;
        std::time_t sec=(std::time_t)(recs[i].exchangeTsUs/1000000); std::tm tmv; gmtime_r(&sec,&tmv);
        std::snprintf(d.UpdateTime,sizeof(d.UpdateTime),"%02d:%02d:%02d",(tmv.tm_hour+8)%24,tmv.tm_min,tmv.tm_sec);
        char td[32]; std::snprintf(td,sizeof(td),"%04d%02d%02d",(tmv.tm_year+1900)%10000,tmv.tm_mon+1,tmv.tm_mday);
        std::memcpy(d.TradingDay,td,sizeof(d.TradingDay));
    }
    std::vector<OutCTP> out(nc);
    double c_best=1e18,e_best=1e18;
    for(int r=0;r<ROUNDS;++r){
        uint64_t t0=__rdtsc(); for(int i=0;i<nc;++i) ctp_decode(&v[i],out[i]); uint64_t t1=__rdtsc();
        double ns=double(t1-t0)/nc/TSC_GHZ; if(ns<c_best)c_best=ns;
        t0=__rdtsc(); for(int i=0;i<nc;++i) ctp_empty(&v[i],out[i]); t1=__rdtsc();
        ns=double(t1-t0)/nc/TSC_GHZ; if(ns<e_best)e_best=ns;
    }
    for(int i=0;i<nc;++i) sink+=out[i].last+out[i].bid1+out[i].hhmmss;

    /* MIRP: 预剥帧 */
    auto pkts=load_pcap(pp);
    if((int)pkts.size()>cap) pkts.resize(cap);
    if(pkts.empty()){std::fprintf(stderr,"pcap 0 packets\n");return 2;}
    std::vector<std::vector<uint8_t>> pay; pay.reserve(pkts.size());
    for(auto& pk:pkts){ wire::View vv{}; if(wire::ParseUdpFrame(pk.data(),pk.size(),vv)) pay.emplace_back(vv.payload,vv.payload+vv.len); }

    double f_best=1e18,w_best=1e18,d_best=1e18; size_t tot_dom=0,tot_bytes=0;
    for(auto& p:pay) tot_bytes+=p.size();
    for(int r=0;r<ROUNDS;++r){
        uint64_t t0=__rdtsc(); size_t acc=0;
        for(auto& pk:pkts){ wire::View vv{}; if(wire::ParseUdpFrame(pk.data(),pk.size(),vv)) acc+=vv.len; }
        uint64_t t1=__rdtsc(); double ns=double(t1-t0)/pkts.size()/TSC_GHZ; if(ns<f_best)f_best=ns; sink+=(double)acc;

        t0=__rdtsc(); Noop np{}; size_t dacc=0;
        for(auto& p:pay) dacc+=(size_t)smdp::IterateDomainsT(p.data(),p.size(),np);
        t1=__rdtsc(); ns=double(t1-t0)/pay.size()/TSC_GHZ; if(ns<w_best)w_best=ns;
        if(r==0) tot_dom=dacc;

        t0=__rdtsc(); Collected col;
        for(auto& p:pay){ col.n=0; smdp::IterateDomainsT(p.data(),p.size(),col); }
        t1=__rdtsc(); ns=double(t1-t0)/pay.size()/TSC_GHZ; if(ns<d_best)d_best=ns;
    }
    double avg_b=(double)tot_bytes/pay.size(), avg_d=(double)tot_dom/pay.size();

    std::printf("== CTP 结构解码 ==\n");
    std::printf("n=%d footprint=%.0fKB | 绝对 %.2f | empty %.2f | 净 %.2f ns/包 | %.3f ns/字段(%d) | %.1f Mops | %.1f cyc/包 | %.2f cyc/store(%d)\n",
        nc, nc*(sizeof(CThostFtdcDepthMarketDataField)+sizeof(OutCTP))/1024.0,
        c_best,e_best,c_best-e_best,(c_best-e_best)/CTP_FIELDS,CTP_FIELDS,
        1000.0/(c_best-e_best),(c_best-e_best)*TSC_GHZ,((c_best-e_best)*TSC_GHZ)/CTP_STORES,CTP_STORES);
    std::printf("== MIRP（预剥帧后）==\n");
    std::printf("n=%zu 平均 %.1fB/包 %.2f域/包\n", pay.size(),avg_b,avg_d);
    std::printf("  帧解析(eth/ip/udp) : %.2f ns/包\n", f_best);
    std::printf("  域遍历(noop)       : %.2f ns/包\n", w_best);
    std::printf("  遍历+解码          : %.2f ns/包 | %.3f ns/域 | %.3f ns/byte | %.1f Mops\n",
                d_best,d_best/avg_d,d_best/avg_b,1000.0/d_best);
    std::printf("  纯解码(遍历+解码-遍历) : %.2f ns/包 | %.3f ns/域\n", d_best-w_best,(d_best-w_best)/avg_d);
    std::printf("sink=%.0f doms=%zu bytes=%zu\n",(double)sink,tot_dom,tot_bytes);
    return 0;
}
