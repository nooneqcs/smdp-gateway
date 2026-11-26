// parse_cmp.cpp —— CTP 回调内解析同口径拆解：full / numonly / empty
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include <random>
#include <algorithm>
#include <x86intrin.h>
#include "ThostFtdcUserApiStruct.h"   // 若名字不同: ls ~/ctp_sdk/md/*.h

static constexpr double TSC_GHZ = 3.0;
static constexpr int    N       = 256;   // 256*584B=149KB，压在 L2 内
static constexpr int    ROUNDS  = 20000;

struct Out64 {
    double  last, bid1, ask1, preSettle, turnover;
    double  open, high, low, upper, lower, preClose;
    int64_t volume, oi, bid1Vol, ask1Vol;
    int32_t hhmmss, tradingDay, pad;
};
static_assert(sizeof(Out64) == 136, "Out64 must be 136B");

static inline int32_t parse_hhmmss(const char* s) {
    return (s[0]-'0')*100000 + (s[1]-'0')*10000 + (s[3]-'0')*1000
         + (s[4]-'0')*100   + (s[6]-'0')*10    + (s[7]-'0');
}
static inline int32_t parse_yyyymmdd(const char* s) {
    return (s[0]-'0')*10000000 + (s[1]-'0')*1000000 + (s[2]-'0')*100000
         + (s[3]-'0')*10000   + (s[4]-'0')*1000    + (s[5]-'0')*100
         + (s[6]-'0')*10      + (s[7]-'0');
}

__attribute__((noinline))
static void ctp_full(const CThostFtdcDepthMarketDataField* p, Out64& o) {
    o.last=p->LastPrice; o.bid1=p->BidPrice1; o.ask1=p->AskPrice1;
    o.preSettle=p->PreSettlementPrice; o.turnover=p->Turnover;
    o.open=p->OpenPrice; o.high=p->HighestPrice; o.low=p->LowestPrice;
    o.upper=p->UpperLimitPrice; o.lower=p->LowerLimitPrice; o.preClose=p->PreClosePrice;
    o.volume=p->Volume; o.oi=(int64_t)p->OpenInterest;
    o.bid1Vol=p->BidVolume1; o.ask1Vol=p->AskVolume1;
    o.hhmmss = parse_hhmmss(p->UpdateTime);
    o.tradingDay = parse_yyyymmdd(p->TradingDay);
    o.pad = 0;
}

__attribute__((noinline))
static void ctp_num(const CThostFtdcDepthMarketDataField* p, Out64& o) {
    o.last=p->LastPrice; o.bid1=p->BidPrice1; o.ask1=p->AskPrice1;
    o.preSettle=p->PreSettlementPrice; o.turnover=p->Turnover;
    o.open=p->OpenPrice; o.high=p->HighestPrice; o.low=p->LowestPrice;
    o.upper=p->UpperLimitPrice; o.lower=p->LowerLimitPrice; o.preClose=p->PreClosePrice;
    o.volume=p->Volume; o.oi=(int64_t)p->OpenInterest;
    o.bid1Vol=p->BidVolume1; o.ask1Vol=p->AskVolume1;
    o.hhmmss = 0; o.tradingDay = 0; o.pad = 0;   // 唯一差别：不碰字符串
}

__attribute__((noinline))
static void ctp_empty(const CThostFtdcDepthMarketDataField*, Out64& o) { o.pad = 0; }

template <class F>
double bench(F f, const std::vector<CThostFtdcDepthMarketDataField>& v,
             std::vector<Out64>& out) {
    double best = 1e18;
    for (int r = 0; r < ROUNDS; ++r) {
        uint64_t t0 = __rdtsc();
        for (int i = 0; i < N; ++i) f(&v[i], out[i]);
        uint64_t t1 = __rdtsc();
        double ns = double(t1 - t0) / N / TSC_GHZ;   // cycles/op ÷ 3.0
        if (ns < best) best = ns;
    }
    return best;
}

int main() {
    std::vector<CThostFtdcDepthMarketDataField> v(N);
    std::mt19937_64 rng(12345);
    for (int i = 0; i < N; ++i) {
        auto& d = v[i];
        std::memset(&d, 0, sizeof(d));
        d.LastPrice = 700.0 + (rng() % 10000) * 0.01;
        d.BidPrice1 = d.LastPrice - 0.02;  d.AskPrice1 = d.LastPrice + 0.02;
        d.PreSettlementPrice = d.LastPrice - 1.0;
        d.OpenPrice = d.LastPrice + 0.5;   d.HighestPrice = d.LastPrice + 2.0;
        d.LowestPrice = d.LastPrice - 2.0; d.UpperLimitPrice = d.LastPrice + 50;
        d.LowerLimitPrice = d.LastPrice - 50; d.PreClosePrice = d.LastPrice - 0.3;
        d.Turnover = (rng() % 1000000) * 1.0;
        d.Volume = int(rng() % 100000);
        d.OpenInterest = double(rng() % 500000);
        d.BidVolume1 = int(rng() % 500);   d.AskVolume1 = int(rng() % 500);
        std::snprintf(d.UpdateTime, sizeof(d.UpdateTime), "%02d:%02d:%02d",
                      9 + int(rng() % 8), int(rng() % 60), int(rng() % 60));
        std::snprintf(d.TradingDay, sizeof(d.TradingDay), "2026%02d%02d",
                      1 + int(rng() % 12), 1 + int(rng() % 28));
    }
    std::vector<Out64> out(N);

    double e  = bench(ctp_empty, v, out);
    double n  = bench(ctp_num,   v, out);
    double f  = bench(ctp_full,  v, out);

    volatile double sink = 0;
    for (int i = 0; i < N; ++i) sink += out[i].last + out[i].bid1 + out[i].hhmmss;
    printf("tsc = %.2f GHz  N=%d rounds=%d\n", TSC_GHZ, N, ROUNDS);
    printf("ctp_empty : %7.2f ns\n", e);
    printf("ctp_num   : %7.2f ns   净 %6.2f\n", n, n - e);
    printf("ctp_full  : %7.2f ns   净 %6.2f   (ASCII 时间 = %6.2f)\n",
           f, f - e, f - n);
    printf("sink=%.0f (防DCE)\n", (double)sink);
    return 0;
}
