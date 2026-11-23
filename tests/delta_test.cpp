#include "delta.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
using namespace smdp;

static int g_fail = 0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++g_fail;} }while(0)

static void put_zz(std::vector<uint8_t>& b, int64_t v) {
    uint64_t zz = (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
    while (zz >= 0x80) { b.push_back(static_cast<uint8_t>((zz & 0x7F) | 0x80)); zz >>= 7; }
    b.push_back(static_cast<uint8_t>(zz));
}

int main() {
    // 0x1002 成交概要：5, 2, -1, 3
    std::vector<uint8_t> s; put_zz(s,5); put_zz(s,2); put_zz(s,-1); put_zz(s,3);
    TradeSummary ts;
    CHECK(DecodeTradeSummary(DomainRef{0x1002,(int16_t)s.size(),s.data()}, ts) == (int)s.size());
    CHECK(ts.LastPriceOffset==5 && ts.VolumeChange==2 && ts.TurnoverOffset==-1 && ts.OpenInterestChange==3);
    printf("0x1002: lastOff=%lld volChg=%lld turnOff=%lld oiChg=%lld\n",
        (long long)ts.LastPriceOffset,(long long)ts.VolumeChange,
        (long long)ts.TurnoverOffset,(long long)ts.OpenInterestChange);

    // 0x1011 单 VInt
    std::vector<uint8_t> one; put_zz(one,7);
    int64_t off = 0;
    CHECK(DecodeSingleVIntOff(DomainRef{0x1011,(int16_t)one.size(),one.data()}, off) && off==7);
    printf("0x1011: off=%lld\n", (long long)off);

    // 0x1018 CurrDelta Double
    uint8_t d8[8]; double dv = 1.5; std::memcpy(d8, &dv, 8);
    double cd = 0;
    CHECK(DecodeCurrDelta(DomainRef{0x1018,8,d8}, cd) && cd==1.5);
    printf("0x1018: currDelta=%.2f\n", cd);

    printf(g_fail ? "FAILED: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
