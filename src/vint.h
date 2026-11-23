#pragma once
#include <cstdint>
#include <cstddef>

namespace smdp {

inline bool DecodeVInt(const uint8_t*& p, const uint8_t* end, uint64_t& out) {
    const uint8_t* q = p;
    if (q >= end) return false;
    uint8_t b = *q++;
    if ((b & 0x80) == 0) { out = b; p = q; return true; }
    uint64_t v = b & 0x7F; int shift = 7;
    while (q < end) {
        b = *q++;
        v |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) { out = v; p = q; return true; }
        shift += 7;
        if (shift >= 70) return false;
    }
    return false;
}

inline bool DecodeSVInt(const uint8_t*& p, const uint8_t* end, int64_t& out) {
    if (p < end && (p[0] & 0x80) == 0) {
        uint64_t z = p[0]; ++p;
        out = static_cast<int64_t>(z >> 1) ^ -static_cast<int64_t>(z & 1);
        return true;
    }
    uint64_t z;
    if (!DecodeVInt(p, end, z)) return false;
    out = static_cast<int64_t>(z >> 1) ^ -static_cast<int64_t>(z & 1);
    return true;
}

inline size_t DecodeVIntBatch(const uint8_t* data, size_t len, uint64_t* out, size_t n) {
    const uint8_t* p = data; const uint8_t* end = data + len; size_t k = 0;
    while (k < n && p < end) { uint64_t v; if (!DecodeVInt(p, end, v)) break; out[k++] = v; }
    return k;
}

} // namespace smdp
