#include "decode.h"
#include "delta.h"
#include <cstdio>
#include <cstdint>

struct Level { double price=0; int64_t vol=0; };
struct Book5 {
    Level   bid[5], ask[5];
    double  last_price=0;
    int64_t cum_vol=0; double cum_turnover=0; long long oi=0;
    double  codec_price=0, tick=0; int64_t volume_multiple=0;
    int64_t last_change_no=0; uint32_t gaps=0;
};

static void apply_level(Book5& b, const smdp::PriceLevelDelta& d) {
    int side = (d.MDEntryType == '0') ? 0 : 1;
    int lv   = (int)d.PriceLevel - 1;
    if (lv < 0 || lv >= 5) return;
    Level* arr = (side == 0) ? b.bid : b.ask;
    if (d.EventType == '3') arr[lv] = {0,0};
    else { arr[lv].price = b.codec_price + (double)d.PriceOffset * b.tick; arr[lv].vol = d.Volume; }
}

static void apply_trade(Book5& b, const smdp::TradeSummary& s) {
    b.last_price = b.codec_price + (double)s.LastPriceOffset * b.tick;
    b.cum_vol   += s.VolumeChange;
    b.cum_turnover += ((double)s.VolumeChange * b.codec_price + (double)s.TurnoverOffset * b.tick) * b.volume_multiple;
    b.oi        += s.OpenInterestChange;
}

static void apply_head(Book5& b, const smdp::ContractDeltaHead& h) {
    if (b.last_change_no != 0 && h.ChangeNo != b.last_change_no + 1) ++b.gaps;
    b.last_change_no = h.ChangeNo;
}

int main() {
    smdp::PriceLevelDelta d{}; smdp::TradeSummary s{}; smdp::ContractDeltaHead h{};

    printf("== 分价表（官方 al1201）==\n");
    Book5 b; b.codec_price=18000; b.tick=5; b.volume_multiple=5;
    d={'1','0',1,0,1}; apply_level(b,d);
    d={'2','0',1,0,2}; apply_level(b,d);
    d={'3','0',1,0,2}; apply_level(b,d);
    d={'1','1',1,0,1}; apply_level(b,d);
    d={'2','1',1,20,1}; apply_level(b,d);
    printf("买一 %.0fx%lld  卖一 %.0fx%lld\n",
        b.bid[0].vol?b.bid[0].price:0.0,(long long)b.bid[0].vol,
        b.ask[0].vol?b.ask[0].price:0.0,(long long)b.ask[0].vol);

    printf("== 成交概要 ==\n");
    Book5 t; t.codec_price=18000; t.tick=5; t.volume_multiple=5; t.oi=1000;
    s={0,4,0,4};  apply_trade(t,s);
    printf("成交1: last=%.0f vol=%lld turnover=%.0f oi=%lld\n",
        t.last_price,(long long)t.cum_vol,t.cum_turnover,(long long)t.oi);
    s={20,4,40,4}; apply_trade(t,s);
    printf("成交2: last=%.0f vol=%lld turnover=%.0f oi=%lld\n",
        t.last_price,(long long)t.cum_vol,t.cum_turnover,(long long)t.oi);

    printf("== ChangeNo 防丢 ==\n");
    Book5 g; g.last_change_no=1;              // 快照 ChangeNo=1
    h={0,2}; apply_head(g,h);
    h={0,3}; apply_head(g,h);
    h={0,4}; apply_head(g,h);
    printf("连号 2,3,4: gaps=%u (期望0)\n", g.gaps);
    h={0,7}; apply_head(g,h);                 // 4->7 跳号
    printf("跳号 4->7 : gaps=%u (期望1)\n", g.gaps);
    return 0;
}

