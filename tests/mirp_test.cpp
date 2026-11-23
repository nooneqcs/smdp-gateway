#include "mirp.h"
#include <cstdio>
using namespace smdp;

// 编解码示例 3.1.1：40 字节报单报文
static const uint8_t kPacket[40] = {
    0x01,0x01,0x10,0x00, 0x02,0x00,0x00,0x00,
    0xE9,0x03,0x00,0x00, 0x02,0x00,0x00,0x00,
    0x77,0x8B,0x0D,0x4F, 0xB4,0x2D,0x00,0x00,
    0x03,0x00,0x02,0x00, 0x00,0x04,
    0x01,0x10,0x06,0x00, 0x31,0x30,0x02,0x00,0x02,0x00
};

struct Printer : DomainVisitor {
    int idx = 0;
    void OnDomain(const DomainRef& d) override {
        printf("  domain[%d] FieldID=0x%04X FieldSize=%d\n",
               idx++, (uint16_t)d.FieldID, d.FieldSize);
    }
};

int main() {
    MirpHeader h;
    if (ParseMirpHeader(kPacket, sizeof(kPacket), h) != 0) { printf("header fail\n"); return 1; }

    printf("Flag=0x%02X TypeID=0x%02X Length=%u PacketNo=%d TopicID=%d "
           "SnapMillisec=%u SnapNo=%d SnapTime=%u CommPhaseNo=%u CenterChangeNo=%d\n",
           h.Flag, (uint8_t)h.TypeID, h.Length, h.PacketNo, h.TopicID,
           h.SnapMillisec, h.SnapNo, h.SnapTime, h.CommPhaseNo, h.CenterChangeNo);

    Printer p;
    int n = IterateDomains(kPacket + sizeof(MirpHeader), h.Length, p);
    printf("domains=%d\n", n);
    return n == 2 ? 0 : 1;
}
