#pragma once
#include <cstdint>
#include <cstddef>

namespace smdp {

#pragma pack(push, 1)
struct MirpHeader {
    uint8_t  Flag;
    int8_t   TypeID;
    uint16_t Length;          // 报文体长度（不含 24B 头）
    int32_t  PacketNo;
    int16_t  TopicID;
    uint16_t SnapMillisec;
    int32_t  SnapNo;
    uint32_t SnapTime;
    uint16_t CommPhaseNo;
    int8_t   CenterChangeNo;
    int8_t   Reserved;
};
#pragma pack(pop)
static_assert(sizeof(MirpHeader) == 24, "MirpHeader must be 24 bytes");

struct DomainRef {
    int16_t FieldID;
    int16_t FieldSize;
    const uint8_t* Body;      // 指向域体
};

// 依赖 x86 小端，直接 memcpy；跨端序需逐字段 bswap
int ParseMirpHeader(const uint8_t* buf, size_t len, MirpHeader& h);

class DomainVisitor {
public:
    virtual void OnDomain(const DomainRef&) = 0;
    virtual ~DomainVisitor() = default;
};

// 返回域个数；长度不一致返回 -1
int IterateDomains(const uint8_t* body, size_t bodyLen, DomainVisitor& v);

} // namespace smdp
