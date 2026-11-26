#include "wire_frame.h"
#include "encode_fast.h"
#include "../src/mirp.h"
#include "../src/decode.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <string>
#include <map>
#include <vector>

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

static inline size_t EncodeReal(uint8_t* out, const RealRec& r)
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

struct Dom { int16_t fid; std::vector<uint8_t> body; };
struct Collect {
    std::vector<Dom>* v;
    void OnDomain(const smdp::DomainRef& d) {
        Dom x; x.fid = d.FieldID;
        x.body.assign(d.Body, d.Body + d.FieldSize);
        v->push_back(std::move(x));
    }
};

static const uint8_t DST_MAC[6] = {0x01,0x00,0x5e,0x00,0x00,0x01};
static const uint8_t SRC_MAC[6] = {0x02,0x00,0x00,0x00,0x00,0x01};
static const uint8_t SRC_IP[4]  = {192,168,99,1};
static const uint8_t DST_IP[4]  = {239,0,0,1};

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "ticks_simnow.bin";
    FILE* f = fopen(path, "rb");
    if (!f) { perror("fopen"); return 1; }
    RecTick t;
    std::vector<RecTick> raw;
    while (fread(&t, sizeof t, 1, f) == 1) raw.push_back(t);
    fclose(f);

    std::map<std::string, int64_t> ino, cn, pv, poi;
    std::map<std::string, double> base, pt;
    std::vector<RealRec> recs;
    for (auto& r : raw) {
        std::string k(r.instrument);
        if (!ino.count(k)) { ino[k] = (int64_t)ino.size() + 1; cn[k] = 0; pv[k] = r.volume;
                             pt[k] = r.turnover; base[k] = r.bid1; poi[k] = (int64_t)llround(r.openInterest); }
        RealRec x{};
        x.ino = ino[k]; x.changeNo = ++cn[k];
        x.bidOff = (int64_t)llround((r.bid1 - base[k]) * 10000.0);
        x.askOff = (int64_t)llround((r.ask1 - base[k]) * 10000.0);
        x.lastOff = (int64_t)llround((r.last - base[k]) * 10000.0);
        x.bidVol = r.bid1Vol; x.askVol = r.ask1Vol;
        x.volChg = r.volume - pv[k]; pv[k] = r.volume;
        x.turnOff = (int64_t)llround(r.turnover - pt[k]); pt[k] = r.turnover;
        x.oiChg = (int64_t)llround(r.openInterest) - poi[k]; poi[k] = (int64_t)llround(r.openInterest);
        recs.push_back(x);
    }
    printf("records=%zu\n", recs.size());

    uint8_t body[512], frame[1024];
    int fails = 0, vlanFails = 0;
    size_t ovh = 0;

    for (size_t i = 0; i < recs.size(); ++i) {
        size_t blen = EncodeReal(body, recs[i]);

        // 无 VLAN
        size_t flen = wire::BuildUdpFrame(frame, body, blen, DST_MAC, SRC_MAC,
                                          SRC_IP, DST_IP, 40000, 1001, 0);
        if (i == 0) ovh = flen - blen;
        wire::View v{};
        if (!wire::ParseUdpFrame(frame, flen, v)) { ++fails; continue; }
        if (v.len != blen || std::memcmp(v.payload, body, blen) != 0) { ++fails; continue; }

        // 带 VLAN
        size_t flen2 = wire::BuildUdpFrame(frame, body, blen, DST_MAC, SRC_MAC,
                                           SRC_IP, DST_IP, 40000, 1001, 100);
        wire::View v2{};
        if (!wire::ParseUdpFrame(frame, flen2, v2)) { ++vlanFails; continue; }
        if (v2.len != blen || std::memcmp(v2.payload, body, blen) != 0) { ++vlanFails; continue; }

        // 截断必须被拒
        if (wire::ParseUdpFrame(frame, flen2 - 5, v2)) { ++fails; }
    }
    printf("frame roundtrip: %s  fails=%d  vlan_fails=%d  overhead=%zu B\n",
           (fails || vlanFails) ? "FAIL" : "ALL PASS", fails, vlanFails, ovh);

    // 接收端视角：解帧 -> 解码 -> 校验第一条
    size_t blen = EncodeReal(body, recs[0]);
    (void)blen;
    size_t flen = wire::BuildUdpFrame(frame, body, blen, DST_MAC, SRC_MAC,
                                      SRC_IP, DST_IP, 40000, 1001, 0);
    wire::View v{};
    wire::ParseUdpFrame(frame, flen, v);
    std::vector<Dom> doms;
    Collect c{&doms};
    smdp::IterateDomainsT(v.payload, v.len, c);
    smdp::ContractDeltaHead h{};
    smdp::DecodeContractHead(smdp::DomainRef{doms[0].fid,(int16_t)doms[0].body.size(),doms[0].body.data()}, h);
    printf("rx: domains=%zu head(ino=%lld,changeNo=%lld) expect(%lld,%lld) -> %s\n",
           doms.size(), (long long)h.InstrumentNo, (long long)h.ChangeNo,
           (long long)recs[0].ino, (long long)recs[0].changeNo,
           (doms.size() == 4 && h.InstrumentNo == recs[0].ino) ? "OK" : "FAIL");
    return (fails || vlanFails) ? 1 : 0;
}
