#pragma once
#include "mirp.h"
#include "decode.h"
#include "delta.h"
#include <cstdint>
#include <vector>

namespace smdp {

void   PutVInt(std::vector<uint8_t>& out, int64_t v);            // zigzag + LEB128
size_t BeginDomain(std::vector<uint8_t>& out, int16_t fieldID);  // 写域头，返回 FieldSize 占位
void   EndDomain(std::vector<uint8_t>& out, size_t sizePos);     // 回填 FieldSize

void EncodeContractHead(std::vector<uint8_t>& out, const ContractDeltaHead& h);   // 0x0003
void EncodePriceLevelDelta(std::vector<uint8_t>& out, const PriceLevelDelta& e);  // 0x1001
void EncodeTradeSummary(std::vector<uint8_t>& out, const TradeSummary& s);        // 0x1002
void EncodeSingleVIntOff(std::vector<uint8_t>& out, int16_t fieldID, int64_t off);// 0x1011~0x1017
void EncodeCurrDelta(std::vector<uint8_t>& out, double cd);                       // 0x1018

std::vector<uint8_t> PackMirpPacket(
    uint8_t typeID, int32_t packetNo, int16_t topicID, int32_t snapNo,
    uint32_t snapTime, uint16_t commPhaseNo, int8_t centerChangeNo,
    const std::vector<uint8_t>& body);

} // namespace smdp

