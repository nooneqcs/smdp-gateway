#include "decode.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
using namespace smdp;

static int g_fail = 0;
#define CHECK(c) do { if(!(c)){ printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while(0)

int main() {
    // 0x0101 合约信息域 112B，按规范偏移构造
    uint8_t b1[112]; std::memset(b1, 0, sizeof(b1));
    std::memcpy(b1 + 0,  "al1202", 6);
    std::memcpy(b1 + 31, "al", 2);
    b1[62] = '1';
    uint64_t dmax = 0x7FEFFFFFFFFFFFFFULL; std::memcpy(b1 + 63, &dmax, 8);
    b1[71] = '0';
    int32_t vm = 5;      std::memcpy(b1 + 72,  &vm, 4);
    double  um = 1.0;    std::memcpy(b1 + 76,  &um, 8);
    int32_t ist = 1;     std::memcpy(b1 + 84,  &ist, 4);
    std::memcpy(b1 + 88, "CNY", 3);
    double  pt = 5.0;    std::memcpy(b1 + 92,  &pt, 8);
    double  cp = 18035.0;std::memcpy(b1 + 100, &cp, 8);
    int32_t ino = 1;     std::memcpy(b1 + 108, &ino, 4);

    ContractInfo ci;
    CHECK(DecodeContractInfo(DomainRef{0x0101, 112, b1}, ci) == 112);
    CHECK(std::memcmp(ci.InstrumentID, "al1202", 6) == 0);
    CHECK(ci.ProductClass == '1');
    CHECK(ci.VolumeMultiple == 5);
    CHECK(ci.PriceTick == 5.0);
    CHECK(ci.CodecPrice == 18035.0);
    CHECK(ci.InstrumentNo == 1);
    printf("0x0101: %s cls=%c vm=%d tick=%.2f codec=%.0f no=%d\n",
           ci.InstrumentID, ci.ProductClass, ci.VolumeMultiple, ci.PriceTick, ci.CodecPrice, ci.InstrumentNo);

    // 0x0102 成交行情域 154B
    uint8_t b2[154]; std::memset(b2, 0, sizeof(b2));
    int32_t ino2 = 0;     std::memcpy(b2 + 0,   &ino2, 4);
    double  last = 18000.0;std::memcpy(b2 + 4,  &last, 8);
    double  oi = 1000.0;  std::memcpy(b2 + 24,  &oi, 8);
    double  up = 18720.0; std::memcpy(b2 + 72,  &up, 8);
    double  lo = 17280.0; std::memcpy(b2 + 80,  &lo, 8);
    std::memcpy(b2 + 128, "20120111", 8);
    std::memcpy(b2 + 137, "21:00:07", 8);
    int32_t ms = 500;     std::memcpy(b2 + 146, &ms, 4);
    int32_t ch = 1;       std::memcpy(b2 + 150, &ch, 4);

    TradeMarketData t;
    CHECK(DecodeTradeMarketData(DomainRef{0x0102, 154, b2}, t) == 154);
    CHECK(t.LastPrice == 18000.0);
    CHECK(t.OpenInterest == 1000.0);
    CHECK(t.UpperLimitPrice == 18720.0);
    CHECK(t.LowerLimitPrice == 17280.0);
    CHECK(t.UpdateMilliSec == 500);
    CHECK(t.ChangeNo == 1);
    printf("0x0102: last=%.0f oi=%.0f up=%.0f lo=%.0f time=%s.%03d chg=%d\n",
           t.LastPrice, t.OpenInterest, t.UpperLimitPrice, t.LowerLimitPrice,
           t.UpdateTime, t.UpdateMilliSec, t.ChangeNo);

    printf(g_fail ? "FAILED: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
