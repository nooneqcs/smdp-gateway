#include "tick2mirp.h"
#include "../src/mirp.h"
#include "../src/decode.h"
#include <cstdio>
#include <vector>

static int failures = 0;

static void ok_rc(int rc, const char* what)
{
    if (rc <= 0) { ++failures; printf("  FAIL %-20s rc=%d\n", what, rc); }
}

static void chk(bool ok, const char* what, long long got, long long exp)
{
    if (!ok) { ++failures; printf("  FAIL %-20s got=%lld exp=%lld\n", what, got, exp); }
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

static smdp::DomainRef Ref(const Dom& d)
{
    return smdp::DomainRef{ d.fid, (int16_t)d.body.size(), d.body.data() };
}

static void TestOne(const bench::SimTick& t, const char* name)
{
    printf("[case] %s\n", name);
    std::vector<uint8_t> pkt = bench::EncodePacket(t, 7);

    smdp::MirpHeader h;
    if (smdp::ParseMirpHeader(pkt.data(), pkt.size(), h) != 0) {
        printf("  FAIL header parse\n"); ++failures; return;
    }
    chk(h.TypeID == 1,   "TypeID",   h.TypeID, 1);
    chk(h.PacketNo == 7, "PacketNo", h.PacketNo, 7);
    chk(h.TopicID == 1001, "TopicID", h.TopicID, 1001);

    std::vector<Dom> doms;
    Collect c{&doms};
    smdp::IterateDomainsT(pkt.data() + sizeof(smdp::MirpHeader),
                          pkt.size() - sizeof(smdp::MirpHeader), c);

    if (doms.size() != 7) {
        ++failures;
        printf("  FAIL domain count=%zu seq:", doms.size());
        for (auto& d : doms) printf(" 0x%04x", (unsigned)d.fid);
        printf("\n");
        return;
    }

    smdp::ContractDeltaHead ch{};
    ok_rc(smdp::DecodeContractHead(Ref(doms[0]), ch), "decode head");
    chk(ch.InstrumentNo == t.ino,   "InstrumentNo", ch.InstrumentNo, t.ino);
    chk(ch.ChangeNo == t.changeNo,  "ChangeNo",     ch.ChangeNo, t.changeNo);

    for (int i = 0; i < 5; ++i) {
        smdp::PriceLevelDelta e{};
        ok_rc(smdp::DecodePriceLevelDelta(Ref(doms[1 + i]), e), "decode level");
        chk(e.PriceLevel  == t.pl[i],    "PriceLevel",  e.PriceLevel,  t.pl[i]);
        chk(e.PriceOffset == t.pxOff[i], "PriceOffset", e.PriceOffset, t.pxOff[i]);
        chk(e.Volume      == t.vol[i],   "Volume",      e.Volume,      t.vol[i]);
        chk((char)e.MDEntryType == t.mdType[i], "MDEntryType", e.MDEntryType, t.mdType[i]);
    }

    smdp::TradeSummary s{};
    ok_rc(smdp::DecodeTradeSummary(Ref(doms[6]), s), "decode trade");
    chk(s.LastPriceOffset   == t.lastOff, "LastPriceOffset",   s.LastPriceOffset,   t.lastOff);
    chk(s.VolumeChange      == t.volChg,  "VolumeChange",      s.VolumeChange,      t.volChg);
    chk(s.TurnoverOffset    == t.turnOff, "TurnoverOffset",    s.TurnoverOffset,    t.turnOff);
    chk(s.OpenInterestChange == t.oiChg,  "OpenInterestChange", s.OpenInterestChange, t.oiChg);
}

int main()
{
    bench::SimTick base{};
    base.ino = 20; base.changeNo = 123456;
    for (int k = 0; k < 5; ++k) {
        base.ev[k] = 0; base.mdType[k] = (char)(k & 1);
        base.pl[k] = 1 + k;
        base.pxOff[k] = 2000 * (k + 1); base.vol[k] = 100 * (k + 1);
    }
    base.lastOff = 0; base.volChg = 10; base.turnOff = 38000000; base.oiChg = -50;
    TestOne(base, "normal");

    const int64_t bounds[] = { 0, 1, -1, 127, 128, -128, -129, 16383, 16384,
                               -16384, 2147483647LL, -2147483648LL,
                               INT64_MAX, INT64_MIN };
    for (size_t i = 0; i < sizeof(bounds)/sizeof(bounds[0]); ++i) {
        bench::SimTick t = base;
        for (int k = 0; k < 5; ++k) { t.pxOff[k] = bounds[i]; t.vol[k] = bounds[i]; }
        t.lastOff = bounds[i]; t.volChg = bounds[i];
        t.turnOff = bounds[i]; t.oiChg = bounds[i];
        char nm[64];
        snprintf(nm, sizeof nm, "vint bound %lld", (long long)bounds[i]);
        TestOne(t, nm);
    }

    if (failures) printf("\n%d FAILURES\n", failures);
    else          printf("\nALL PASS\n");
    return failures ? 1 : 0;
}
