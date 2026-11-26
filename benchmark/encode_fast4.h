#pragma once
#include "tick2mirp.h"
#include <cstdint>
#include <cstring>

namespace fastenc4 {

inline __attribute__((always_inline)) uint8_t* PutVIntSlow(uint8_t* p, uint64_t zz)
{
    while (zz >= 0x80) { *p++ = (uint8_t)((zz & 0x7F) | 0x80); zz >>= 7; }
    *p++ = (uint8_t)zz;
    return p;
}

inline __attribute__((always_inline)) uint8_t* PutVInt(uint8_t* p, int64_t v)
{
    uint64_t zz = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
    if (zz >= 0x200000ull) return PutVIntSlow(p, zz);   // 极少数，分支可预测

    unsigned d1 = (unsigned)(zz >= 0x80ull);
    unsigned d2 = (unsigned)(zz >= 0x4000ull);

    uint32_t w = (uint32_t)(zz & 0x7Full)
               | (uint32_t)(((zz >> 7)  & 0x7Full) << 8)
               | (uint32_t)(((zz >> 14) & 0x7Full) << 16);
    w |= (d1 ? 0x80u : 0u) | (d2 ? 0x8000u : 0u);

    std::memcpy(p, &w, 4);
    return p + 1 + d1 + d2;
}

inline __attribute__((always_inline)) uint8_t* BeginDomain(uint8_t* p, int16_t fid, uint8_t** sizePos)
{
    std::memcpy(p, &fid, 2); *sizePos = p + 2; return p + 4;
}

inline __attribute__((always_inline)) uint8_t* EndDomain(uint8_t* p, uint8_t* sizePos)
{
    uint16_t sz = (uint16_t)((p - sizePos) - 2);
    std::memcpy(sizePos, &sz, 2);
    return p;
}

inline __attribute__((always_inline)) size_t EncodeBodyFast(uint8_t* out, const bench::SimTick& t)
{
    uint8_t* p = out;
    uint8_t* sz;

    p = BeginDomain(p, 0x0003, &sz);
    p = PutVInt(p, t.ino);
    p = PutVInt(p, t.changeNo);
    p = EndDomain(p, sz);

    for (int i = 0; i < 5; ++i) {
        p = BeginDomain(p, 0x1001, &sz);
        uint16_t em = (uint16_t)((uint8_t)t.ev[i] | ((uint16_t)(uint8_t)t.mdType[i] << 8));
        std::memcpy(p, &em, 2); p += 2;
        p = PutVInt(p, t.pl[i]);
        p = PutVInt(p, t.pxOff[i]);
        p = PutVInt(p, t.vol[i]);
        p = EndDomain(p, sz);
    }

    p = BeginDomain(p, 0x1002, &sz);
    p = PutVInt(p, t.lastOff);
    p = PutVInt(p, t.volChg);
    p = PutVInt(p, t.turnOff);
    p = PutVInt(p, t.oiChg);
    p = EndDomain(p, sz);

    return (size_t)(p - out);
}

inline __attribute__((always_inline)) size_t EncodePacketFast(uint8_t* out, const bench::SimTick& t, int32_t packetNo)
{
    static const uint8_t kHdr[24] = {
        0x01,0x01,0x00,0x00, 0x00,0x00,0x00,0x00, 0xE9,0x03, 0x00,0x00,
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00, 0x00,0x00
    };
    std::memcpy(out, kHdr, 24);
    std::memcpy(out + 4, &packetNo, 4);
    size_t blen = EncodeBodyFast(out + 24, t);
    uint16_t l16 = (uint16_t)blen;
    std::memcpy(out + 2, &l16, 2);
    return 24 + blen;
}

} // namespace fastenc4
