#pragma once
// 交易端占位实现：策略恒真 -> 构造 -> 风控 -> 状态机 -> 账本(本地模拟成交)
// 换成真实引擎时，只改 trade_on_tick() 内部四步
#include <cstdio>
#include <cstdint>

struct TradeStats { unsigned long long orders=0, rejects=0, fills=0; };
static TradeStats g_trade;

template<class T>
static inline void trade_on_tick(const T& t){
    if (t.bid <= 0.0 || t.ask <= 0.0) { ++g_trade.rejects; return; }  // 无盘口
    if (t.ask <  t.bid)               { ++g_trade.rejects; return; }  // 交叉
    double px = t.bid;          // 1)策略恒真 2)以 bid1 挂买 1 手
    ++g_trade.orders;           // 3)风控通过(偏离0/手数1) 4)状态机 SUBMITTED
    ++g_trade.fills;            // 5)账本: 本地立即成交
    if ((g_trade.orders + g_trade.rejects) % 50 == 0)
        fprintf(stderr, "[trade] orders=%llu rejects=%llu\n",
                g_trade.orders, g_trade.rejects);
}
__attribute__((destructor)) static void trade_dump(){
    fprintf(stderr, "[trade-final] orders=%llu rejects=%llu fills=%llu\n",
            g_trade.orders, g_trade.rejects, g_trade.fills);
}
