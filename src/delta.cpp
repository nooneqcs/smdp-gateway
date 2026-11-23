#include "delta.h"
#include "vint.h"
#include <cstring>

namespace smdp {

static inline int64_t zz(uint8_t b){ return (int64_t(b) >> 1) ^ -(int64_t(b) & 1); }

int DecodeTradeSummary(const DomainRef& d, TradeSummary& out) {
    // 快路径：4 个 1 字节 VInt，读 4B 一次
    if (d.FieldSize >= 4) {
        uint32_t w; std::memcpy(&w, d.Body, 4);
        if ((w & 0x80808080u) == 0) {
            out.LastPriceOffset    = zz((uint8_t)(w));
            out.VolumeChange       = zz((uint8_t)(w >> 8));
            out.TurnoverOffset     = zz((uint8_t)(w >> 16));
            out.OpenInterestChange = zz((uint8_t)(w >> 24));
            return 4;
        }
    }
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
