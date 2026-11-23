#pragma once
#include "mirp.h"
#include <cstdint>

namespace smdp {

struct ContractDeltaHead {
    int64_t InstrumentNo;
    int64_t ChangeNo;
};

struct PriceLevelDelta {
    char    EventType;    // '1'增 '2'改 '3'删
    char    MDEntryType;  // '0'bid '1'ask
    int64_t PriceLevel;
    int64_t PriceOffset;
    int64_t Volume;
};

// 返回消耗的域体字节数；失败 <0
int DecodeContractHead(const DomainRef& d, ContractDeltaHead& out);
int DecodePriceLevelDelta(const DomainRef& d, PriceLevelDelta& out);

} // namespace smdp
