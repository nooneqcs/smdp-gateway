#include "delta.h"
#include "vint.h"
#include <cstring>

namespace smdp {

int DecodeTradeSummary(const DomainRef& d, TradeSummary& out) {
    const uint8_t* p = d.Body;
    const uint8_t* end = d.Body + d.FieldSize;
    if (!DecodeSVInt(p, end, out.LastPriceOffset))    return -1;
    if (!DecodeSVInt(p, end, out.VolumeChange))       return -1;
    if (!DecodeSVInt(p, end, out.TurnoverOffset))     return -1;
    if (!DecodeSVInt(p, end, out.OpenInterestChange)) return -1;
    return static_cast<int>(p - d.Body);
}

bool DecodeSingleVIntOff(const DomainRef& d, int64_t& out) {
    const uint8_t* p = d.Body;
    const uint8_t* end = d.Body + d.FieldSize;
    return DecodeSVInt(p, end, out);
}

bool DecodeCurrDelta(const DomainRef& d, double& out) {
    if (d.FieldSize < 8) return false;
    std::memcpy(&out, d.Body, 8);
    return true;
}

} // namespace smdp
