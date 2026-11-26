#pragma once
#include "tick2mirp.h"
#include <cstdint>
#include <cstring>

namespace fastenc2 {

static const uint64_t kCont[9] = {
    0ull, 0ull, 0x80ull, 0x8080ull, 0x808080ull,
    0x80808080ull, 0x8080808080ull, 0x808080808080ull,
    0x80808080808080ull
};

inline uint8_t* PutVInt(uint8_t* p, int64_t v)
{
    uint64_t zz = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
    unsigned bits = 64u - (unsigned)__builtin_clzll(zz | 1u);
    unsigned len  = (bits + 6u) / 7u;

    uint64_t x = (zz & 0x7Full)
               | (((zz >> 7)  & 0x7Full) << 8)
               | (((zz >> 14) & 0x7Full) << 16)
               | (((zz >> 21) & 0x7Full) << 24)
               | (((zz >> 28) & 0x7Full) << 32)
               | (((zz >> 35) & 0x7Full) << 40)
               | (((zz >> 42) & 0x7Full) << 48)
               | (((zz >> 49) & 0x7Full) << 56);

    if (len <= 8) {
        x |= kCont[len];
        std::memcpy(p, &x, 8);
        return p + len;
    }
    x |= 0x8080808080808080ull;
    std::memcpy(p, &x, 8);
    p[8] = (uint8_t)((zz >> 56) & 0x7F);
    if (len == 9) return p + 9;
    p[8] |= 0x80;
    p[9] = (uint8_t)((zz >> 63) & 0x7F);
    return p + 10;
}

inline uint8_t* BeginDomain(uint8_t* p, int16_t fid, uint8_t** sizePos)
{
    std::memcpy(p, &fid, 2);
    *sizePos = p + 2;
    return p + 4;
}

inline uint8_t* EndDomain(uint8_t* p, uint8_t* sizePos)
{
    uint16_t sz = (uint16_t)((p - sizePos) - 2);
    std::memcpy(sizePos, &sz, 2);
    return p;
}

inline size_t EncodeBodyFast(uint8_t* out, const bench::SimTick& t)
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

inline size_t EncodePacketFast(uint8_t* out, const bench::SimTick& t, int32_t packetNo)
{
    static const uint8_t kHdr[24] = {
        0x01, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0xE9, 0x03,
        0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00,
        0x00, 0x00
    };
    std::memcpy(out, kHdr, 24);
    std::memcpy(out + 4, &packetNo, 4);

    size_t blen = EncodeBodyFast(out + 24, t);
    uint16_t l16 = (uint16_t)blen;
    std::memcpy(out + 2, &l16, 2);
    return 24 + blen;
}

} // namespace fastenc2
