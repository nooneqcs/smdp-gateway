#include "encode_fast.h"
#include "../src/mirp.h"
#include "../src/decode.h"
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <ctime>
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
    int64_t ino, changeNo;
    int64_t bidOff, askOff, lastOff;
    int64_t bidVol, askVol, volChg, turnOff, oiChg;
};

static volatile uint64_t g_sink = 0;
static inline uint64_t now_ns() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static inline size_t EncodeReal(uint8_t* out, const RealRec& r)
{
    uint8_t* p = out;
    uint8_t* sz;

    p = fastenc::BeginDomain(p, 0x0003, &sz);
    p = fastenc::PutVInt(p, r.ino);
    p = fastenc::PutVInt(p, r.changeNo);
    p = fastenc::EndDomain(p, sz);

    p = fastenc::BeginDomain(p, 0x1001, &sz);
    *p++ = 0; *p++ = 0;                       // EventType, MDEntryType=bid
    p = fastenc::PutVInt(p, 1);               // PriceLevel
    p = fastenc::PutVInt(p, r.bidOff);
    p = fastenc::PutVInt(p, r.bidVol);
    p = fastenc::EndDomain(p, sz);

    p = fastenc::BeginDomain(p, 0x1001, &sz);
    *p++ = 0; *p++ = 1;                       // ask
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

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "ticks_simnow.bin";
    FILE* f = fopen(path, "rb");
    if (!f) { perror("fopen"); return 1; }

    RecTick t;
    std::vector<RecTick> raw;
    while (fread(&t, sizeof t, 1, f) == 1) raw.push_back(t);
    fclose(f);
    printf("loaded %zu ticks\n", raw.size());

    std::map<std::string, int64_t> ino;
    std::map<std::string, int64_t> changeNo, prevVol;
    std::map<std::string, double>  prevTurn;
    std::map<std::string, int64_t> prevOi;
    std::map<std::string, double>  base;
    std::vector<RealRec> recs;
    long zeroBidVol = 0;

    for (auto& r : raw) {
        std::string k(r.instrument);
        if (!ino.count(k)) { ino[k] = (int64_t)ino.size() + 1; changeNo[k] = 0; prevVol[k] = r.volume; prevTurn[k] = r.turnover; base[k] = r.bid1; prevOi[k] = (int64_t)llround(r.openInterest); }
        if (r.bid1Vol <= 0) zeroBidVol++;

        RealRec x{};
        x.ino      = ino[k];
        x.changeNo = ++changeNo[k];
        x.bidOff   = (int64_t)llround((r.bid1 - base[k]) * 10000.0);
        x.askOff   = (int64_t)llround((r.ask1 - base[k]) * 10000.0);
        x.lastOff  = (int64_t)llround((r.last - base[k]) * 10000.0);
        x.bidVol   = r.bid1Vol;
        x.askVol   = r.ask1Vol;
        x.volChg   = r.volume - prevVol[k];  prevVol[k] = r.volume;
        x.turnOff  = (int64_t)llround(r.turnover - prevTurn[k]); prevTurn[k] = r.turnover;
        x.oiChg    = (int64_t)llround(r.openInterest) - prevOi[k]; prevOi[k] = (int64_t)llround(r.openInterest);
        recs.push_back(x);
    }
    printf("records=%zu  instruments=%zu  zeroBidVol=%ld\n", recs.size(), ino.size(), zeroBidVol);

    uint8_t buf[512];
    size_t minLen = 999, maxLen = 0, sumLen = 0;
    int fails = 0;

    for (size_t i = 0; i < recs.size(); ++i) {
        size_t len = EncodeReal(buf, recs[i]);
        minLen = len < minLen ? len : minLen;
        maxLen = len > maxLen ? len : maxLen;
        sumLen += len;

        std::vector<Dom> doms;
        Collect c{&doms};
        smdp::IterateDomainsT(buf, len, c);
        if (doms.size() != 4) { ++fails; printf("FAIL domain count %zu @%zu\n", doms.size(), i); continue; }

        smdp::ContractDeltaHead h{};
        if (smdp::DecodeContractHead(smdp::DomainRef{doms[0].fid,(int16_t)doms[0].body.size(),doms[0].body.data()}, h) <= 0) { ++fails; continue; }
        if (h.InstrumentNo != recs[i].ino || h.ChangeNo != recs[i].changeNo) { ++fails; printf("FAIL head @%zu\n", i); continue; }

        smdp::PriceLevelDelta b{};
        if (smdp::DecodePriceLevelDelta(smdp::DomainRef{doms[1].fid,(int16_t)doms[1].body.size(),doms[1].body.data()}, b) <= 0) { ++fails; continue; }
        if (b.PriceOffset != recs[i].bidOff || b.Volume != recs[i].bidVol) { ++fails; printf("FAIL bid @%zu\n", i); continue; }

        smdp::PriceLevelDelta a{};
        if (smdp::DecodePriceLevelDelta(smdp::DomainRef{doms[2].fid,(int16_t)doms[2].body.size(),doms[2].body.data()}, a) <= 0) { ++fails; continue; }
        if (a.PriceOffset != recs[i].askOff || a.Volume != recs[i].askVol) { ++fails; printf("FAIL ask @%zu\n", i); continue; }

        smdp::TradeSummary s{};
        if (smdp::DecodeTradeSummary(smdp::DomainRef{doms[3].fid,(int16_t)doms[3].body.size(),doms[3].body.data()}, s) <= 0) { ++fails; continue; }
        if (s.LastPriceOffset != recs[i].lastOff || s.VolumeChange != recs[i].volChg) { ++fails; printf("FAIL trade @%zu\n", i); continue; }
    }
    printf("roundtrip: %s  fails=%d\n", fails ? "FAIL" : "ALL PASS", fails);
    printf("bytes: min=%zu avg=%.1f max=%zu\n", minLen, (double)sumLen / recs.size(), maxLen);
    if (fails) return 1;

    const size_t N = 2000000;
    for (int round = 0; round < 3; ++round) {
        uint64_t t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            size_t len = EncodeReal(buf, recs[i % recs.size()]);
            g_sink += len + buf[0];
        }
        uint64_t t1 = now_ns();
        printf("[real 4-domain] %.2f ns/pkt  %.1f Mpps\n",
               (double)(t1 - t0) / N, (double)N / (t1 - t0) * 1000.0);
    }
    printf("sink=%llu\n", (unsigned long long)g_sink);
    return 0;
}
