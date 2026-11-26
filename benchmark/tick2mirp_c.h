#pragma once
#include "encode_fast.h"
#include <cstdint>
#include <cstring>

namespace benchc {

// 64 字节，1 cache line。字段用能装下的最窄类型，避免 3 条 cache line
struct SimTickC {
    int64_t turnOff;                                  //  0.. 7
    int32_t changeNo, ino, lastOff, volChg, oiChg;    //  8..27
    int16_t pxOff[5];                                 // 28..37
    int16_t vol[5];                                   // 38..47
    int8_t  pl[5];                                    // 48..52
    uint8_t ev[5];                                    // 53..57
    uint8_t mdType[5];                                // 58..62
    uint8_t _pad;                                     // 63
};
static_assert(sizeof(SimTickC) == 64, "SimTickC must be 64 bytes");

inline size_t EncodeBodyC(uint8_t* out, const SimTickC& t)
{
    uint8_t* p = out;
    uint8_t* sz;

    p = fastenc::BeginDomain(p, 0x0003, &sz);
    p = fastenc::PutVInt(p, t.ino);
    p = fastenc::PutVInt(p, t.changeNo);
    p = fastenc::EndDomain(p, sz);

    for (int i = 0; i < 5; ++i) {
        p = fastenc::BeginDomain(p, 0x1001, &sz);
        *p++ = t.ev[i];
        *p++ = t.mdType[i];
        p = fastenc::PutVInt(p, t.pl[i]);
        p = fastenc::PutVInt(p, t.pxOff[i]);
        p = fastenc::PutVInt(p, t.vol[i]);
        p = fastenc::EndDomain(p, sz);
    }

    p = fastenc::BeginDomain(p, 0x1002, &sz);
    p = fastenc::PutVInt(p, t.lastOff);
    p = fastenc::PutVInt(p, t.volChg);
    p = fastenc::PutVInt(p, t.turnOff);
    p = fastenc::PutVInt(p, t.oiChg);
    p = fastenc::EndDomain(p, sz);

    return (size_t)(p - out);
}

} // namespace benchc
