#include "mirp.h"
#include "decode.h"
#include <cstdio>
using namespace smdp;

static const uint8_t kPacket[40] = {
    0x01,0x01,0x10,0x00, 0x02,0x00,0x00,0x00,
    0xE9,0x03,0x00,0x00, 0x02,0x00,0x00,0x00,
    0x77,0x8B,0x0D,0x4F, 0xB4,0x2D,0x00,0x00,
    0x03,0x00,0x02,0x00, 0x00,0x04,
    0x01,0x10,0x06,0x00, 0x31,0x30,0x02,0x00,0x02,0x00
};

static int g_fail = 0;
#define CHECK(c) do { if(!(c)){ printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++g_fail; } } while(0)

struct Decoder : DomainVisitor {
    int n_head = 0, n_level = 0;
    void OnDomain(const DomainRef& d) override {
        if (d.FieldID == 0x0003) {
            ContractDeltaHead h;
            int used = DecodeContractHead(d, h);
            printf("  0x0003 InstrumentNo=%lld ChangeNo=%lld (used %d/%d)\n",
                   (long long)h.InstrumentNo, (long long)h.ChangeNo, used, d.FieldSize);
            CHECK(used == d.FieldSize);
            CHECK(h.InstrumentNo == 0);
            CHECK(h.ChangeNo == 2);
            ++n_head;
        } else if (d.FieldID == 0x1001) {
            PriceLevelDelta l;
            int used = DecodePriceLevelDelta(d, l);
            printf("  0x1001 Event=%c MDEntry=%c Level=%lld Offset=%lld Vol=%lld (used %d/%d)\n",
                   l.EventType, l.MDEntryType, (long long)l.PriceLevel,
                   (long long)l.PriceOffset, (long long)l.Volume, used, d.FieldSize);
            CHECK(l.EventType == '1');
            CHECK(l.MDEntryType == '0');
            CHECK(l.PriceLevel == 1);
            CHECK(l.PriceOffset == 0);
            CHECK(l.Volume == 1);
            ++n_level;
        }
    }
};

int main() {
    MirpHeader h;
    CHECK(ParseMirpHeader(kPacket, sizeof(kPacket), h) == 0);
    CHECK(h.PacketNo == 2 && h.TopicID == 1001 && h.SnapNo == 2 && h.Length == 16);
    printf("header: PacketNo=%d TopicID=%d SnapNo=%d Length=%u\n",
           h.PacketNo, h.TopicID, h.SnapNo, h.Length);

    Decoder dec;
    int n = IterateDomains(kPacket + sizeof(MirpHeader), h.Length, dec);
    CHECK(n == 2);
    CHECK(dec.n_head == 1 && dec.n_level == 1);

    printf(g_fail ? "FAILED: %d\n" : "ALL PASS\n", g_fail);
    return g_fail ? 1 : 0;
}
