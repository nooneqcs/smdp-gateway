#pragma once
#include "mirp.h"
#include <cstdint>

namespace smdp {

struct TradeSummary {                 // 0x1002
    int64_t LastPriceOffset, VolumeChange, TurnoverOffset, OpenInterestChange;
};

int  DecodeTradeSummary(const DomainRef& d, TradeSummary& out);  // 0x1002
bool DecodeSingleVIntOff(const DomainRef& d, int64_t& out);      // 0x1011~0x1017（单个 VInt 偏移）
bool DecodeCurrDelta(const DomainRef& d, double& out);           // 0x1018（Double）

} // namespace smdp
