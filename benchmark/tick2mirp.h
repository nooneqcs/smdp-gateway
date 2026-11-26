#pragma once
#include "../src/encode.h"
#include <cstdint>
#include <vector>

namespace bench {

struct SimTick {
    int64_t ino, changeNo;
    int64_t lastOff, volChg, turnOff, oiChg;
    char    ev[5], mdType[5];
    int64_t pl[5];       // PriceLevel
    int64_t pxOff[5];    // PriceOffset
    int64_t vol[5];
};

inline void EncodeBody(std::vector<uint8_t>& out, const SimTick& t)
{
    smdp::ContractDeltaHead h;
    h.InstrumentNo = t.ino;
    h.ChangeNo     = t.changeNo;
    smdp::EncodeContractHead(out, h);

    for (int i = 0; i < 5; ++i) {
        smdp::PriceLevelDelta e{};
        e.EventType   = t.ev[i];
        e.MDEntryType = t.mdType[i];
        e.PriceLevel  = t.pl[i];
        e.PriceOffset = t.pxOff[i];
        e.Volume      = t.vol[i];
        smdp::EncodePriceLevelDelta(out, e);
    }

    smdp::TradeSummary s{};
    s.LastPriceOffset    = t.lastOff;
    s.VolumeChange       = t.volChg;
    s.TurnoverOffset     = t.turnOff;
    s.OpenInterestChange = t.oiChg;
    smdp::EncodeTradeSummary(out, s);
}

inline std::vector<uint8_t> EncodePacket(const SimTick& t, int32_t packetNo)
{
    std::vector<uint8_t> body;
    body.reserve(128);
    EncodeBody(body, t);
    return smdp::PackMirpPacket(1, packetNo, 1001, 0, 0, 0, 0, body);
}

} // namespace bench
