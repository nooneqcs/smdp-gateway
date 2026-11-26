// parse_cmp2.cpp —— CTP 回调解析四方同口径（v2: +sscanf版 +全字段sink +N可调）
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <random>
#include <algorithm>
#include <x86intrin.h>
#include "ThostFtdcUserApiStruct.h"

static constexpr double TSC_GHZ = 3.0;

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

#define NUM_FIELDS(p,o) \
    (o).last=(p)->LastPrice; (o).bid1=(p)->BidPrice1; (o).ask1=(p)->AskPrice1; \
    (o).preSettle=(p)->PreSettlementPrice; (o).turnover=(p)->Turnover; \
    (o).open=(p)->OpenPrice; (o).high=(p)->HighestPrice; (o).low=(p)->LowestPrice; \
    (o).upper=(p)->UpperLimitPrice; (o).lower=(p)->LowerLimitPrice; (o).preClose=(p)->PreClosePrice; \
    (o).volume=(p)->Volume; (o).oi=(int64_t)(p)->OpenInterest; \
    (o).bid1Vol=(p)->BidVolume1; (o).ask1Vol=(p)->AskVolume1; (o).pad=0

__attribute__((noinline)) static void ctp_num(const CThostFtdcDepthMarketDataField* p, Out64& o) {
    NUM_FIELDS(p,o); o.hhmmss=0; o.tradingDay=0;
}
__attribute__((noinline)) static void ctp_full(const CThostFtdcDepthMarketDataField* p, Out64& o) {
    NUM_FIELDS(p,o);
    o.hhmmss = parse_hhmmss(p->UpdateTime);
    o.tradingDay = parse_yyyymmdd(p->TradingDay);
}
__attribute__((noinline)) static void ctp_sscanf(const CThostFtdcDepthMarketDataField* p, Out64& o) {
    NUM_FIELDS(p,o);
    int h=0,m=0,s=0;
    std::sscanf(p->UpdateTime, "%d:%d:%d", &h,&m,&s);
    o.hhmmss = h*10000 + m*100 + s;
    o.tradingDay = std::atoi(p->TradingDay);
}
__attribute__((noinline)) static void ctp_empty(const CThostFtdcDepthMarketDataField*, Out64& o) {
    o.pad=0; o.hhmmss=0;
}

template <class F>
static double bench(F f, const std::vector<CThostFtdcDepthMarketDataField>& v,
                    std::vector<Out64>& out, int rounds) {
    int n = (int)v.size();
    double best = 1e18;
    for (int r = 0; r < rounds; ++r) {
        uint64_t t0 = __rdtsc();
        for (int i = 0; i < n; ++i) f(&v[i], out[i]);
        uint64_t t1 = __rdtsc();
        double ns = double(t1 - t0) / n / TSC_GHZ;
        if (ns < best) best = ns;
    }
    return best;
}

int main(int argc, char** argv) {
    int N      = argc > 1 ? std::atoi(argv[1]) : 256;
    int ROUNDS = argc > 2 ? std::atoi(argv[2]) : 20000;

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

    double e = bench(ctp_empty,  v, out, ROUNDS);
    double n = bench(ctp_num,    v, out, ROUNDS);
    double f = bench(ctp_full,   v, out, ROUNDS);
    double s = bench(ctp_sscanf, v, out, ROUNDS);

    volatile double sink = 0;
    for (int i = 0; i < N; ++i) {
        const Out64& o = out[i];
        sink += o.last+o.bid1+o.ask1+o.preSettle+o.turnover+o.open+o.high+o.low
              + o.upper+o.lower+o.preClose+o.volume+o.oi+o.bid1Vol+o.ask1Vol
              + o.hhmmss+o.tradingDay;
    }
    printf("tsc=%.2fGHz  N=%d  rounds=%d  footprint=%.1f KB\n",
           TSC_GHZ, N, ROUNDS, N*(double)(sizeof(CThostFtdcDepthMarketDataField)+sizeof(Out64))/1024.0);
    printf("ctp_empty  : %8.2f ns\n", e);
    printf("ctp_num    : %8.2f ns  净 %8.2f   (20字段, 无时间串)\n", n, n-e);
    printf("ctp_full   : %8.2f ns  净 %8.2f   (手写ASCII +%5.2f)\n", f, f-e, f-n);
    printf("ctp_sscanf : %8.2f ns  净 %8.2f   (sscanf  +%5.2f)\n", s, s-e, s-n);
    printf("sscanf/手写 = %.1fx\n", (s-e)/(f-e));
    printf("sink=%.0f\n", (double)sink);
    return 0;
}
