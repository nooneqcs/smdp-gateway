#include "book.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
using namespace smdp;

static int g_fail=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++g_fail;} }while(0)

int main() {
    Book b;
    // 快照乱序装载，Finalize 后应排序
    b.LoadSnapshotLevel(PriceBookLevel{20,'0',17995.0,2});
    b.LoadSnapshotLevel(PriceBookLevel{20,'0',18000.0,1});
    b.LoadSnapshotLevel(PriceBookLevel{20,'1',18005.0,3});
    b.LoadSnapshotLevel(PriceBookLevel{20,'1',18010.0,4});
    b.Finalize();
    CHECK(b.Bid[0].Price==18000.0 && b.Bid[1].Price==17995.0);   // bid 降序
    CHECK(b.Ask[0].Price==18005.0 && b.Ask[1].Price==18010.0);   // ask 升序

    // 增量：改 bid1量5 / 删 bid2 / ask2 价18010量4
    b.ApplyDelta(PriceLevelDelta{'2','0',1,0,5}, 18000.0, 5.0);
    b.ApplyDelta(PriceLevelDelta{'3','0',2,0,0}, 18000.0, 5.0);
    b.ApplyDelta(PriceLevelDelta{'1','1',2,2,4}, 18000.0, 5.0);
    CHECK(b.Bid[0].Volume==5);
    CHECK(b.Bid[1].Volume==0);
    CHECK(b.Ask[1].Price==18010.0 && b.Ask[1].Volume==4);
    printf("after apply:\n"); b.Dump();

    // 0x0103 解码
    uint8_t raw[17];
    int32_t ino=20; std::memcpy(raw,&ino,4); raw[4]='0';
    double px=18000.0; std::memcpy(raw+5,&px,8);
    int32_t vol=7; std::memcpy(raw+13,&vol,4);
    PriceBookLevel lv;
    CHECK(DecodePriceBookLevel(DomainRef{0x0103,17,raw}, lv));
    CHECK(lv.InstrumentNo==20 && lv.Direction=='0' && lv.Price==18000.0 && lv.Volume==7);

    printf(g_fail?"FAILED: %d\n":"ALL PASS\n", g_fail);
    return g_fail?1:0;
}
