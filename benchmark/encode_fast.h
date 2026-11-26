#pragma once
#include "tick2mirp.h"
#include <cstdint>
#include <cstring>

namespace fastenc {

inline uint8_t* PutVInt(uint8_t* p, int64_t v)
{
    uint64_t zz = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
    while (zz >= 0x80) { *p++ = (uint8_t)(zz | 0x80); zz >>= 7; }
    *p++ = (uint8_t)zz;
    return p;
}

inline uint8_t* BeginDomain(uint8_t* p, int16_t fid, uint8_t** sizePos)
{
    *p++ = (uint8_t)(fid & 0xFF);
    *p++ = (uint8_t)((fid >> 8) & 0xFF);
    *sizePos = p;
    p += 2;
    return p;
}

inline uint8_t* EndDomain(uint8_t* p, uint8_t* sizePos)
{
    uint16_t sz = (uint16_t)((p - sizePos) - 2);
    sizePos[0] = (uint8_t)(sz & 0xFF);
    sizePos[1] = (uint8_t)(sz >> 8);
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
        *p++ = (uint8_t)t.ev[i];
        *p++ = (uint8_t)t.mdType[i];
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
    uint8_t* p = out;
    *p++ = 0x01;                       // Flag
    *p++ = 0x01;                       // TypeID
    uint8_t* lenPos = p; p += 2;       // Length 占位
    std::memcpy(p, &packetNo, 4); p += 4;
    int16_t  topic = 1001; std::memcpy(p, &topic, 2);  p += 2;
    uint16_t ms    = 0;    std::memcpy(p, &ms, 2);     p += 2;
    int32_t  snapNo = 0;   std::memcpy(p, &snapNo, 4); p += 4;
    uint32_t snapT  = 0;   std::memcpy(p, &snapT, 4);  p += 4;
    uint16_t phase  = 0;   std::memcpy(p, &phase, 2);  p += 2;
    *p++ = 0;                          // CenterChangeNo
    *p++ = 0;                          // Reserved

    size_t blen = EncodeBodyFast(p, t);
    uint16_t l16 = (uint16_t)blen;
    std::memcpy(lenPos, &l16, 2);
    return (size_t)sizeof(smdp::MirpHeader) + blen;
}

} // namespace fastenc
