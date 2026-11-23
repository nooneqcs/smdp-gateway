#include "vint.h"

namespace smdp {

bool DecodeVInt(const uint8_t*& p, const uint8_t* end, uint64_t& out) {
    uint64_t v = 0;
    int shift = 0;
    const uint8_t* q = p;
    while (q < end) {
        uint8_t b = *q++;
        if (shift < 64) v |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) { out = v; p = q; return true; }
        shift += 7;
        if (shift >= 70) return false;   // >10 字节畸形
    }
    return false;                        // 截断
}

bool DecodeSVInt(const uint8_t*& p, const uint8_t* end, int64_t& out) {
    uint64_t z;
    if (!DecodeVInt(p, end, z)) return false;
    out = static_cast<int64_t>(z >> 1) ^ -static_cast<int64_t>(z & 1);
    return true;
}

size_t DecodeVIntBatch(const uint8_t* data, size_t len, uint64_t* out, size_t n) {
    const uint8_t* p = data;
    const uint8_t* end = data + len;
    size_t k = 0;
    while (k < n && p < end) {
        uint64_t v;
        if (!DecodeVInt(p, end, v)) break;
        out[k++] = v;
    }
    return k;
}

} // namespace smdp
