#include "book.h"
#include <cstring>
#include <cstdio>
#include <algorithm>

namespace smdp {

bool DecodePriceBookLevel(const DomainRef& d, PriceBookLevel& out) {
    if (d.FieldSize < 17) return false;
    const uint8_t* p = d.Body;
    std::memcpy(&out.InstrumentNo, p, 4); p += 4;
    out.Direction = static_cast<char>(*p++);
    std::memcpy(&out.Price, p, 8); p += 8;
    std::memcpy(&out.Volume, p, 4); p += 4;
    return true;
}

void Book::LoadSnapshotLevel(const PriceBookLevel& lv) {
    Level* side = (lv.Direction == '0') ? Bid : Ask;
    for (int i = 0; i < kDepth; ++i) {
        if (side[i].Volume == 0) { side[i].Price = lv.Price; side[i].Volume = lv.Volume; return; }
    }
}

void Book::ApplyDelta(const PriceLevelDelta& ev, double codecPrice, double priceTick) {
    Level* side = (ev.MDEntryType == '0') ? Bid : Ask;       // 方向
    unsigned i = static_cast<unsigned>(ev.PriceLevel) - 1u;  // 档位
    if (i >= static_cast<unsigned>(kDepth)) return;          // 超深度
    const bool del = (ev.EventType == '3');                  // 删除用 cmov
    side[i].Price  = del ? 0.0 : (codecPrice + static_cast<double>(ev.PriceOffset) * priceTick);
    side[i].Volume = del ? int64_t(0) : ev.Volume;
}

void Book::Finalize() {
    auto bidCmp = [](const Level&a,const Level&b){ bool ea=a.Volume==0, eb=b.Volume==0; if(ea!=eb) return !ea; return a.Price>b.Price; };
    auto askCmp = [](const Level&a,const Level&b){ bool ea=a.Volume==0, eb=b.Volume==0; if(ea!=eb) return !ea; return a.Price<b.Price; };
    std::sort(Bid, Bid+kDepth, bidCmp);
    std::sort(Ask, Ask+kDepth, askCmp);
}

void Book::Dump() const {
    printf("  bid:"); for (int i=0;i<kDepth;++i) printf(" %.1f/%lld", Bid[i].Price,(long long)Bid[i].Volume);
    printf("\n  ask:"); for (int i=0;i<kDepth;++i) printf(" %.1f/%lld", Ask[i].Price,(long long)Ask[i].Volume);
    printf("\n");
}

} // namespace smdp

