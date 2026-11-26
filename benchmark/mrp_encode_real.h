#pragma once
#include "encode_fast.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <string>
#include <map>
#include <vector>

namespace realdata {

struct RecTick {
    char    instrument[16];
    int64_t exchangeTsUs;
    double  last, bid1, ask1, preSettle;
    int64_t bid1Vol, ask1Vol, volume;
    double  turnover, openInterest;
};

struct RealRec {
    int64_t ino, changeNo, bidOff, askOff, lastOff, bidVol, askVol, volChg, turnOff, oiChg;
};

inline bool Load(const char* path, std::vector<RecTick>& out)
{
    FILE* f = fopen(path, "rb");
    if (!f) return false;
    RecTick t;
    while (fread(&t, sizeof t, 1, f) == 1) out.push_back(t);
    fclose(f);
    return true;
}

inline void Build(const std::vector<RecTick>& raw, std::vector<RealRec>& recs)
{
    std::map<std::string, int64_t> ino, cn, pv, poi;
    std::map<std::string, double>  base, pt;
    for (auto& r : raw) {
        std::string k(r.instrument);
        if (!ino.count(k)) {
            ino[k] = (int64_t)ino.size() + 1; cn[k] = 0;
            pv[k] = r.volume; pt[k] = r.turnover;
            base[k] = r.bid1; poi[k] = (int64_t)llround(r.openInterest);
        }
        RealRec x{};
        x.ino = ino[k]; x.changeNo = ++cn[k];
        x.bidOff  = (int64_t)llround((r.bid1  - base[k]) * 10000.0);
        x.askOff  = (int64_t)llround((r.ask1  - base[k]) * 10000.0);
        x.lastOff = (int64_t)llround((r.last  - base[k]) * 10000.0);
        x.bidVol = r.bid1Vol; x.askVol = r.ask1Vol;
        x.volChg = r.volume - pv[k]; pv[k] = r.volume;
        x.turnOff = (int64_t)llround(r.turnover - pt[k]); pt[k] = r.turnover;
        x.oiChg = (int64_t)llround(r.openInterest) - poi[k]; poi[k] = (int64_t)llround(r.openInterest);
        recs.push_back(x);
    }
}

inline size_t EncodeReal(uint8_t* out, const RealRec& r)
{
    uint8_t* p = out; uint8_t* sz;
    p = fastenc::BeginDomain(p, 0x0003, &sz);
    p = fastenc::PutVInt(p, r.ino);
    p = fastenc::PutVInt(p, r.changeNo);
    p = fastenc::EndDomain(p, sz);

    p = fastenc::BeginDomain(p, 0x1001, &sz);
    *p++ = 0; *p++ = 0;
    p = fastenc::PutVInt(p, 1);
    p = fastenc::PutVInt(p, r.bidOff);
    p = fastenc::PutVInt(p, r.bidVol);
    p = fastenc::EndDomain(p, sz);

    p = fastenc::BeginDomain(p, 0x1001, &sz);
    *p++ = 0; *p++ = 1;
    p = fastenc::PutVInt(p, 1);
    p = fastenc::PutVInt(p, r.askOff);
    p = fastenc::PutVInt(p, r.askVol);
    p = fastenc::EndDomain(p, sz);

    p = fastenc::BeginDomain(p, 0x1002, &sz);
    p = fastenc::PutVInt(p, r.lastOff);
    p = fastenc::PutVInt(p, r.volChg);
    p = fastenc::PutVInt(p, r.turnOff);
    p = fastenc::PutVInt(p, r.oiChg);
    p = fastenc::EndDomain(p, sz);
    return (size_t)(p - out);
}

} // namespace realdata
