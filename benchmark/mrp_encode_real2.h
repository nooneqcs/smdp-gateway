#pragma once
#include "encode_fast.h"
#include <cstdio>
#include <cstdint>
#include <cmath>
#include <cstring>
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
    double  codecPrice, priceTick;
    int32_t volMultiple;
    int8_t  first;                 // 1 = 本合约首帧，附带 0x0101
};

// 与 src/decode.h 的 ContractInfo 逐字节一致（112B, pack(1)）
#pragma pack(push, 1)
struct Info0101 {
    char    InstrumentID[31];
    char    UnderlyingInstrID[31];
    char    ProductClass;
    double  StrikePrice;
    char    OptionsType;
    int32_t VolumeMultiple;
    double  UnderlyingMultiple;
    int32_t IsTrading;
    char    CurrencyID[4];
    double  PriceTick;
    double  CodecPrice;
    int32_t InstrumentNo;
};
#pragma pack(pop)
static_assert(sizeof(Info0101) == 112, "ContractInfo 必须是 112B");

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
    std::map<std::string, bool>    seen;
    for (auto& r : raw) {
        std::string k(r.instrument);
        if (!ino.count(k)) {
            ino[k] = (int64_t)ino.size() + 1; cn[k] = 0;
            pv[k] = r.volume; pt[k] = r.turnover;
            base[k] = r.bid1; poi[k] = (int64_t)llround(r.openInterest);
        }
        const double tick = 0.01;                 // 重建精度；价格是 2 位小数就无损
        RealRec x{};
        x.ino = ino[k]; x.changeNo = ++cn[k];
        x.codecPrice = base[k]; x.priceTick = tick; x.volMultiple = 1;
        x.first = seen[k] ? 0 : 1; seen[k] = true;
        x.bidOff  = (int64_t)llround((r.bid1 - base[k]) / tick);
        x.askOff  = (int64_t)llround((r.ask1 - base[k]) / tick);
        x.lastOff = (int64_t)llround((r.last - base[k]) / tick);
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

    if (r.first) {                                  // 0x0101 合约信息（112B 定长）
        Info0101 ci{};
        ci.InstrumentNo   = (int32_t)r.ino;
        ci.VolumeMultiple = r.volMultiple;
        ci.PriceTick      = r.priceTick;
        ci.CodecPrice     = r.codecPrice;
        ci.IsTrading      = 1;
        p = fastenc::BeginDomain(p, 0x0101, &sz);
        std::memcpy(p, &ci, sizeof(ci)); p += sizeof(ci);
        p = fastenc::EndDomain(p, sz);
    }

    p = fastenc::BeginDomain(p, 0x1001, &sz);       // 买
    *p++ = '2'; *p++ = '0';
    p = fastenc::PutVInt(p, 1);
    p = fastenc::PutVInt(p, r.bidOff);
    p = fastenc::PutVInt(p, r.bidVol);
    p = fastenc::EndDomain(p, sz);

    p = fastenc::BeginDomain(p, 0x1001, &sz);       // 卖
    *p++ = '2'; *p++ = '1';
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
