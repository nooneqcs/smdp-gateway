#pragma once
#include "decode.h"
#include <cstdint>

namespace smdp {

struct Level {
    double  Price  = 0.0;
    int64_t Volume = 0;
};

struct PriceBookLevel {          // 0x0103 快照分价表域（17B）
    int32_t InstrumentNo;
    char    Direction;           // '0' bid, '1' ask
    double  Price;
    int32_t Volume;
};

class Book {
public:
    static constexpr int kDepth = 5;
    Level Bid[kDepth];
    Level Ask[kDepth];

    void LoadSnapshotLevel(const PriceBookLevel& lv);                                 // 0x0103
    void ApplyDelta(const PriceLevelDelta& ev, double codecPrice, double priceTick);  // 0x1001
    void Finalize();     // bid 降序 / ask 升序
    void Dump() const;
};

bool DecodePriceBookLevel(const DomainRef& d, PriceBookLevel& out);   // 0x0103

} // namespace smdp
