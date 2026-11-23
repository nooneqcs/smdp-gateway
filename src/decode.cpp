#include "decode.h"
#include "vint.h"
#include <cstring>

namespace smdp {

int DecodeContractHead(const DomainRef& d, ContractDeltaHead& out) {
    const uint8_t* p = d.Body;
    const uint8_t* end = d.Body + d.FieldSize;
    if (!DecodeSVInt(p, end, out.InstrumentNo)) return -1;
    if (!DecodeSVInt(p, end, out.ChangeNo)) return -1;
    return static_cast<int>(p - d.Body);
}

int DecodePriceLevelDelta(const DomainRef& d, PriceLevelDelta& out) {
    const uint8_t* p = d.Body;
    const uint8_t* end = d.Body + d.FieldSize;
    if (p + 2 > end) return -1;
    out.EventType   = static_cast<char>(*p++);
    out.MDEntryType = static_cast<char>(*p++);
    if (!DecodeSVInt(p, end, out.PriceLevel))  return -1;
    if (!DecodeSVInt(p, end, out.PriceOffset)) return -1;
    if (!DecodeSVInt(p, end, out.Volume))      return -1;
    return static_cast<int>(p - d.Body);
}

int DecodeContractInfo(const DomainRef& d, ContractInfo& out) {
    if (d.FieldSize < static_cast<int>(sizeof(ContractInfo))) return -1;
    std::memcpy(&out, d.Body, sizeof(ContractInfo));
    return static_cast<int>(sizeof(ContractInfo));
}

int DecodeTradeMarketData(const DomainRef& d, TradeMarketData& out) {
    if (d.FieldSize < static_cast<int>(sizeof(TradeMarketData))) return -1;
    std::memcpy(&out, d.Body, sizeof(TradeMarketData));
    return static_cast<int>(sizeof(TradeMarketData));
}

} // namespace smdp
