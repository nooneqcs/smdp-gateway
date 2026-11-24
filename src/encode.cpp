#include "encode.h"
#include <cstring>

namespace smdp {

void PutVInt(std::vector<uint8_t>& out, int64_t v) {
    uint64_t zz = (static_cast<uint64_t>(v) << 1) ^ static_cast<uint64_t>(v >> 63);
    while (zz >= 0x80) { out.push_back(static_cast<uint8_t>((zz & 0x7F) | 0x80)); zz >>= 7; }
    out.push_back(static_cast<uint8_t>(zz));
}

size_t BeginDomain(std::vector<uint8_t>& out, int16_t fid) {
    out.push_back(static_cast<uint8_t>(fid & 0xFF));
    out.push_back(static_cast<uint8_t>((fid >> 8) & 0xFF));
    size_t pos = out.size();
    out.push_back(0); out.push_back(0);      // FieldSize 占位
    return pos;
}

void EndDomain(std::vector<uint8_t>& out, size_t pos) {
    int16_t sz = static_cast<int16_t>(out.size() - pos - 2);
    out[pos]     = static_cast<uint8_t>(sz & 0xFF);
    out[pos + 1] = static_cast<uint8_t>((sz >> 8) & 0xFF);
}

void EncodeContractHead(std::vector<uint8_t>& out, const ContractDeltaHead& h) {
    size_t p = BeginDomain(out, 0x0003);
    PutVInt(out, h.InstrumentNo);
    PutVInt(out, h.ChangeNo);
    EndDomain(out, p);
}

void EncodePriceLevelDelta(std::vector<uint8_t>& out, const PriceLevelDelta& e) {
    size_t p = BeginDomain(out, 0x1001);
    out.push_back(static_cast<uint8_t>(e.EventType));
    out.push_back(static_cast<uint8_t>(e.MDEntryType));
    PutVInt(out, e.PriceLevel);
    PutVInt(out, e.PriceOffset);
    PutVInt(out, e.Volume);
    EndDomain(out, p);
}

void EncodeTradeSummary(std::vector<uint8_t>& out, const TradeSummary& s) {
    size_t p = BeginDomain(out, 0x1002);
    PutVInt(out, s.LastPriceOffset);
    PutVInt(out, s.VolumeChange);
    PutVInt(out, s.TurnoverOffset);
    PutVInt(out, s.OpenInterestChange);
    EndDomain(out, p);
}

void EncodeSingleVIntOff(std::vector<uint8_t>& out, int16_t fid, int64_t off) {
    size_t p = BeginDomain(out, fid);
    PutVInt(out, off);
    EndDomain(out, p);
}

void EncodeCurrDelta(std::vector<uint8_t>& out, double cd) {
    size_t p = BeginDomain(out, 0x1018);
    uint8_t b[8]; std::memcpy(b, &cd, 8);
    out.insert(out.end(), b, b + 8);
    EndDomain(out, p);
}

std::vector<uint8_t> PackMirpPacket(
    uint8_t typeID, int32_t packetNo, int16_t topicID, int32_t snapNo,
    uint32_t snapTime, uint16_t commPhaseNo, int8_t centerChangeNo,
    const std::vector<uint8_t>& body) {
    MirpHeader h{};
    h.Flag = 0x01; h.TypeID = static_cast<int8_t>(typeID);
    h.Length = static_cast<uint16_t>(body.size());
    h.PacketNo = packetNo; h.TopicID = topicID; h.SnapMillisec = 0;
    h.SnapNo = snapNo; h.SnapTime = snapTime; h.CommPhaseNo = commPhaseNo;
    h.CenterChangeNo = centerChangeNo; h.Reserved = 0;
    std::vector<uint8_t> pkt(sizeof(MirpHeader));
    std::memcpy(pkt.data(), &h, sizeof(MirpHeader));
    pkt.insert(pkt.end(), body.begin(), body.end());
    return pkt;
}

} // namespace smdp

