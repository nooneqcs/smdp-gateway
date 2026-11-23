#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>

namespace smdp {

#pragma pack(push, 1)
struct MirpHeader {
    uint8_t  Flag;
    int8_t   TypeID;
    uint16_t Length;
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
    const uint8_t* Body;
};

int ParseMirpHeader(const uint8_t* buf, size_t len, MirpHeader& h);

class DomainVisitor {
public:
    virtual void OnDomain(const DomainRef&) = 0;
    virtual ~DomainVisitor() = default;
};

int IterateDomains(const uint8_t* body, size_t bodyLen, DomainVisitor& v);

// 模板版：编译期具体化，去虚调用
template <typename Visitor>
inline int IterateDomainsT(const uint8_t* body, size_t bodyLen, Visitor& v) {
    size_t off = 0; int n = 0;
    while (off + 4 <= bodyLen) {
        int16_t fid, fsz;
        std::memcpy(&fid, body + off, 2);
        std::memcpy(&fsz, body + off + 2, 2);
        off += 4;
        if (fsz < 0 || off + static_cast<size_t>(fsz) > bodyLen) return -1;
        v.OnDomain(DomainRef{fid, fsz, body + off});
        off += static_cast<size_t>(fsz);
        ++n;
    }
    return (off == bodyLen) ? n : -1;
}

} // namespace smdp
