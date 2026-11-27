#pragma once
#include <cstdio>
#include <cstdint>

struct TradeStats { unsigned long long orders=0, fills=0, rej_noquote=0, rej_cross=0; };
static TradeStats g_trade;

template<class T>
static inline void trade_on_tick(const T& t){
    unsigned long long n = g_trade.orders + g_trade.rej_noquote + g_trade.rej_cross + 1;
    if (t.bid <= 0.0 || t.ask <= 0.0)      ++g_trade.rej_noquote;
    else if (t.ask < t.bid)                ++g_trade.rej_cross;
    else { ++g_trade.orders; ++g_trade.fills; }
    if (n <= 5)
        fprintf(stderr, "[trade] n=%llu ino=%lld bid=%.4f ask=%.4f noq=%llu cross=%llu ord=%llu\n",
                n, (long long)t.ino, t.bid, t.ask,
                g_trade.rej_noquote, g_trade.rej_cross, g_trade.orders);
}
__attribute__((destructor)) static void trade_dump(){
    fprintf(stderr, "[trade-final] orders=%llu fills=%llu noquote=%llu cross=%llu\n",
            g_trade.orders, g_trade.fills, g_trade.rej_noquote, g_trade.rej_cross);
}
