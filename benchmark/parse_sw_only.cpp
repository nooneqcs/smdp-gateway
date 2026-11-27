// parse_sw_only.cpp —— 纯软件解码基准：CTP 结构 vs MIRP（无网络/无DPDK/无内核）
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

/* ================= CTP 侧 ================= */
#pragma pack(push,1)
struct RecTick { char instrument[16]; int64_t exchangeTsUs; double last,bid1,ask1,preSettle;
                 int64_t bid1Vol,ask1Vol,volume; double turnover,openInterest; };
#pragma pack(pop)
static_assert(sizeof(RecTick)==96, "RecTick");

struct OutCTP { char instrument[16]; double last,bid1,ask1,preSettle,turnover,openInterest;
                int64_t volume,bid1Vol,ask1Vol; double open,high,low,upper,lower,preClose;
                int32_t hhmmss,tradingDay; };
static_assert(sizeof(OutCTP)==144, "OutCTP");
static constexpr int CTP_FIELDS = 17, CTP_STORES = 18;

static inline int32_t ph(const char* s){return (s[0]-'0')*100000+(s[1]-'0')*10000+(s[3]-'0')*1000
                                        +(s[4]-'0')*100+(s[6]-'0')*10+(s[7]-'0');}
static inline int32_t py(const char* s){return (s[0]-'0')*10000000+(s[1]-'0')*1000000+(s[2]-'0')*100000
                                        +(s[3]-'0')*10000+(s[4]-'0')*1000+(s[5]-'0')*100
                                        +(s[6]-'0')*10+(s[7]-'0');}

__attribute__((noinline))
static void ctp_decode(const CThostFtdcDepthMarketDataField* p, OutCTP& o){
    std::memcpy(o.instrument, p->InstrumentID, 16);
    o.last=p->LastPrice; o.bid1=p->BidPrice1; o.ask1=p->AskPrice1;
    o.preSettle=p->PreSettlementPrice; o.turnover=p->Turnover; o.openInterest=p->OpenInterest;
    o.volume=p->Volume; o.bid1Vol=p->BidVolume1; o.ask1Vol=p->AskVolume1;
    o.open=p->OpenPrice; o.high=p->HighestPrice; o.low=p->LowestPrice;
    o.upper=p->UpperLimitPrice; o.lower=p->LowerLimitPrice; o.preClose=p->PreClosePrice;
    o.hhmmss=ph(p->UpdateTime); o.tradingDay=py(p->TradingDay);
}

/* ================= MIRP 侧 ================= */
struct Collected {
    smdp::ContractDeltaHead H{};
    smdp::PriceLevelDelta   PL{};
    smdp::TradeSummary      TS{};
    int n = 0;
    void OnDomain(const smdp::DomainRef& d){
        switch (d.FieldID) {
            case 0x0003: smdp::DecodeContractHead(d, H);   break;
            case 0x1001: smdp::DecodePriceLevelDelta(d, PL); break;
            case 0x1002: smdp::DecodeTradeSummary(d, TS);  break;
        }
        ++n;
    }
};

static std::vector<std::vector<uint8_t>> load_pcap(const char* path){
    std::vector<std::vector<uint8_t>> out;
    FILE* f=std::fopen(path,"rb"); if(!f){std::perror("pcap");return out;}
    uint8_t gh[24];
    if(std::fread(gh,1,24,f)!=24){std::fclose(f);return out;}
    uint32_t magic; std::memcpy(&magic, gh, 4);
    bool swap = (magic==0xd4c3b2a1u || magic==0x4d3cb2a1u);
    for(;;){
        uint8_t ph[16]; if(std::fread(ph,1,16,f)!=16) break;
        uint32_t raw; std::memcpy(&raw, ph+8, 4);
        uint32_t incl = swap ? __builtin_bswap32(raw) : raw;
        if(incl>65535u) break;
        std::vector<uint8_t> buf(incl);
        if(std::fread(buf.data(),1,incl,f)!=incl) break;
        out.push_back(std::move(buf));
    }
    std::fclose(f); return out;
}

int main(int argc, char** argv){
    const char* tick_path = argc>1 ? argv[1] : "ticks_simnow.bin";
    const char* pcap_path = argc>2 ? argv[2] : "mirp_out.pcap";
    int cap = argc>3 ? std::atoi(argv[3]) : 256;

    /* ---- CTP ---- */
    FILE* f=std::fopen(tick_path,"rb"); if(!f){std::perror("ticks");return 1;}
    std::fseek(f,0,SEEK_END); long sz=std::ftell(f); std::fseek(f,0,SEEK_SET);
    int n_rec=(int)(sz/sizeof(RecTick));
    std::vector<RecTick> recs(n_rec);
    if(std::fread(recs.data(),sizeof(RecTick),n_rec,f)!=(size_t)n_rec){std::fprintf(stderr,"ticks read\n");return 1;}
    std::fclose(f);
    int nc = std::min(n_rec, cap);
    std::vector<CThostFtdcDepthMarketDataField> v(nc);
    for(int i=0;i<nc;++i){
        auto& d=v[i]; std::memset(&d,0,sizeof(d));
        std::memcpy(d.InstrumentID, recs[i].instrument, 16);
        d.LastPrice=recs[i].last; d.BidPrice1=recs[i].bid1; d.AskPrice1=recs[i].ask1;
        d.PreSettlementPrice=recs[i].preSettle;
        d.BidVolume1=(int)recs[i].bid1Vol; d.AskVolume1=(int)recs[i].ask1Vol; d.Volume=(int)recs[i].volume;
        d.Turnover=recs[i].turnover; d.OpenInterest=recs[i].openInterest;
        std::time_t sec=(std::time_t)(recs[i].exchangeTsUs/1000000);
        std::tm tmv; gmtime_r(&sec,&tmv);
        std::snprintf(d.UpdateTime,sizeof(d.UpdateTime),"%02d:%02d:%02d",(tmv.tm_hour+8)%24,tmv.tm_min,tmv.tm_sec);
        std::snprintf(d.TradingDay,sizeof(d.TradingDay),"%04d%02d%02d",tmv.tm_year+1900,tmv.tm_mon+1,tmv.tm_mday);
    }
    std::vector<OutCTP> out(nc);
    double c_best=1e18;
    for(int r=0;r<ROUNDS;++r){
        uint64_t t0=__rdtsc();
        for(int i=0;i<nc;++i) ctp_decode(&v[i], out[i]);
        uint64_t t1=__rdtsc();
        double ns=double(t1-t0)/nc/TSC_GHZ; if(ns<c_best)c_best=ns;
    }
    volatile double sink=0;
    for(int i=0;i<nc;++i) sink+=out[i].last+out[i].bid1+out[i].ask1+out[i].preSettle+out[i].turnover
                              +out[i].openInterest+out[i].volume+out[i].bid1Vol+out[i].ask1Vol
                              +out[i].open+out[i].high+out[i].low+out[i].upper+out[i].lower
                              +out[i].preClose+out[i].hhmmss+out[i].tradingDay;

    /* ---- MIRP ---- */
    auto pkts = load_pcap(pcap_path);
    if((int)pkts.size()>cap) pkts.resize(cap);
    if(pkts.empty()){ std::fprintf(stderr,"pcap 0 packets (检查 magic/路径)\n"); return 2; }

    size_t tot_bytes=0, tot_dom=0;
    for(auto& pk: pkts){
        wire::View vv{};
        if(!wire::ParseUdpFrame(pk.data(), pk.size(), vv)) continue;
        tot_bytes += vv.len;
        Collected tmp;
        tot_dom += (size_t)smdp::IterateDomainsT(vv.payload, vv.len, tmp);
    }

    Collected col;   // 复用，避免每包零初始化
    double m_best=1e18;
    for(int r=0;r<ROUNDS;++r){
        uint64_t t0=__rdtsc();
        for(size_t i=0;i<pkts.size();++i){
            col.n = 0;
            wire::View vv{};
            if(!wire::ParseUdpFrame(pkts[i].data(), pkts[i].size(), vv)) continue;
            smdp::IterateDomainsT(vv.payload, vv.len, col);
        }
        uint64_t t1=__rdtsc();
        double ns=double(t1-t0)/pkts.size()/TSC_GHZ; if(ns<m_best)m_best=ns;
    }
    double avg_bytes = (double)tot_bytes/pkts.size();
    double avg_dom   = (double)tot_dom  /pkts.size();

    std::printf("== CTP 结构解码（纯软件）==\n");
    std::printf("n=%d footprint=%.0fKB | %.2f ns/包 | %.3f ns/字段(%d) | %.1f Mops | %.1f cyc/包 | %.2f cyc/store(%d)\n",
        nc, nc*(sizeof(CThostFtdcDepthMarketDataField)+sizeof(OutCTP))/1024.0,
        c_best, c_best/CTP_FIELDS, CTP_FIELDS, 1000.0/c_best, c_best*TSC_GHZ, c_best*TSC_GHZ/CTP_STORES, CTP_STORES);
    std::printf("store 下限(1/cyc)=%.2f ns -> 达成 %.0f%%\n\n", CTP_STORES/TSC_GHZ, CTP_STORES/(c_best*TSC_GHZ)*100.0);

    std::printf("== MIRP 解码（纯软件）==\n");
    std::printf("n=%zu 平均 %.1f B/包 %.2f 域/包 | %.2f ns/包 | %.3f ns/域 | %.3f ns/byte | %.1f Mops\n",
        pkts.size(), avg_bytes, avg_dom, m_best, m_best/avg_dom, m_best/avg_bytes, 1000.0/m_best);
    std::printf("sink=%.0f doms=%zu bytes=%zu\n",(double)sink,tot_dom,tot_bytes);
    return 0;
}

