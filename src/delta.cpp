#include "delta.h"
#include "vint.h"
#include <cstring>
#include <smmintrin.h>   // SSE4.1

namespace smdp {

static inline int64_t zz(uint8_t b){ return (int64_t(b) >> 1) ^ -(int64_t(b) & 1); }

int DecodeTradeSummary(const DomainRef& d, TradeSummary& out) {
    // SIMD：4 个 1 字节 VInt（读 4B，向量化 zigzag）
    if (d.FieldSize >= 4) {
        __m128i b = _mm_cvtsi32_si128(*reinterpret_cast<const int32_t*>(d.Body));
        if (_mm_movemask_epi8(b) == 0) {              // 4 字节都无 bit7
            __m128i x   = _mm_cvtepu8_epi32(b);       // 4 × int32
            __m128i one = _mm_set1_epi32(1);
            __m128i z   = _mm_xor_si128(_mm_srli_epi32(x, 1),
                                        _mm_sub_epi32(_mm_setzero_si128(), _mm_and_si128(x, one)));
            alignas(16) int32_t r[4];
            _mm_store_si128(reinterpret_cast<__m128i*>(r), z);
            out.LastPriceOffset    = r[0];
            out.VolumeChange       = r[1];
            out.TurnoverOffset     = r[2];
            out.OpenInterestChange = r[3];
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
