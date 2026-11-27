#include "decode.h"          // smdp::PriceLevelDelta / ContractDeltaHead
#include <cstdio>
#include <cstdint>

struct Level { double price=0; int64_t vol=0; };
struct Book5 {
    Level   bid[5], ask[5];          // vol==0 视为空档
    double  last_price=0;
    int64_t cum_vol=0; double cum_turnover=0; long long oi=0;
    double  codec_price=0, tick=0; int64_t volume_multiple=0;
    int64_t last_change_no=0; uint32_t gaps=0;
};

// 应用一笔分价表变动
static void apply_level(Book5& b, const smdp::PriceLevelDelta& d) {
    int side = (d.MDEntryType == '0') ? 0 : 1;   // '0'=买 '1'=卖
    int lv   = (int)d.PriceLevel - 1;            // 1-based -> 0-based
    if (lv < 0 || lv >= 5) return;
    Level* arr = (side == 0) ? b.bid : b.ask;
    if (d.EventType == '3') {                    // 删除
        arr[lv] = {0, 0};
    } else {                                     // '1'增加 / '2'修改
        arr[lv].price = b.codec_price + (double)d.PriceOffset * b.tick;
        arr[lv].vol   = d.Volume;
    }
}

static void show(const char* tag, const Book5& b) {
    printf("%-8s 买一 %8.0f x %-3lld | 卖一 %8.0f x %-3lld\n", tag,
        b.bid[0].vol ? b.bid[0].price : 0.0, (long long)b.bid[0].vol,
        b.ask[0].vol ? b.ask[0].price : 0.0, (long long)b.ask[0].vol);
}

int main() {
    Book5 b; b.codec_price = 18000; b.tick = 5; b.volume_multiple = 5;
    smdp::PriceLevelDelta d{};

    // 官方示例 al1201 的操作序列
    d = {'1','0',1,0,1}; apply_level(b,d); show("增加买一", b);   // 18000 x1
    d = {'2','0',1,0,2}; apply_level(b,d); show("改买一量", b);   // 18000 x2
    d = {'3','0',1,0,2}; apply_level(b,d); show("删买一",   b);   // 空
    d = {'1','1',1,0,1}; apply_level(b,d); show("增加卖一", b);   // 18000 x1
    d = {'2','1',1,20,1}; apply_level(b,d); show("改卖一价", b);  // 18100 x1
    return 0;
}

