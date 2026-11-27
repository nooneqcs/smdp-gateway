#pragma once
#include <cstdio>
#include <cstdint>
#include <algorithm>
#include <rte_cycles.h>

struct TradeStats { unsigned long long orders=0, fills=0, rej_noquote=0, rej_cross=0; };
static TradeStats g_trade;

static const int kT6Max = 1<<20;
static uint64_t g_t6[kT6Max];
static int g_t6n = 0;

template<class T>
static inline void trade_on_tick(const T& t){
    if (t.bid <= 0.0 || t.ask <= 0.0)      ++g_trade.rej_noquote;
    else if (t.ask < t.bid)                ++g_trade.rej_cross;
    else { ++g_trade.orders; ++g_trade.fills; }

    uint64_t d = rte_rdtsc_precise() - t.t_prod;    // 入队→订单就绪
    if (g_t6n < kT6Max) g_t6[g_t6n++] = d;
}

__attribute__((destructor)) static void trade_dump(){
    fprintf(stderr, "[trade-final] orders=%llu fills=%llu noquote=%llu cross=%llu n=%d\n",
            g_trade.orders, g_trade.fills, g_trade.rej_noquote, g_trade.rej_cross, g_t6n);
    if (g_t6n > 0){
        std::sort(g_t6, g_t6 + g_t6n);
        double k = 1e9 / (double)rte_get_tsc_hz();
        auto P=[&](double p)->double{ size_t i=(size_t)((double)g_t6n*p/100.0);
                                       if(i>=(size_t)g_t6n) i=g_t6n-1; return (double)g_t6[i]*k; };
        fprintf(stderr, "t6 入队→订单: p50=%.1f p99=%.1f p999=%.1f ns\n", P(50), P(99), P(999));
    }
}
