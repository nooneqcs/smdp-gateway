#include "tick2mirp.h"
#include "encode_fast.h"
#include <cstdio>
#include <cstring>
#include <vector>

static int fails = 0;

static void One(const bench::SimTick& t, const char* name)
{
    std::vector<uint8_t> slow; slow.reserve(256);
    bench::EncodeBody(slow, t);

    uint8_t buf[512];
    size_t n = fastenc::EncodeBodyFast(buf, t);
    if (!(n == slow.size() && std::memcmp(buf, slow.data(), n) == 0)) {
        ++fails;
        printf("FAIL body %-22s fast=%zu slow=%zu\n", name, n, slow.size());
    }

    std::vector<uint8_t> full = bench::EncodePacket(t, 7);
    size_t m = fastenc::EncodePacketFast(buf, t, 7);
    if (!(m == full.size() && std::memcmp(buf, full.data(), m) == 0)) {
        ++fails;
        printf("FAIL full %-22s fast=%zu slow=%zu\n", name, m, full.size());
    }
}

int main()
{
    bench::SimTick base{};
    base.ino = 20; base.changeNo = 123456;
    for (int k = 0; k < 5; ++k) {
        base.ev[k] = 0; base.mdType[k] = (char)(k & 1);
        base.pl[k] = 1 + k; base.pxOff[k] = 2000 * (k + 1); base.vol[k] = 100 * (k + 1);
    }
    base.lastOff = 0; base.volChg = 10; base.turnOff = 38000000; base.oiChg = -50;
    One(base, "normal");

    const int64_t bounds[] = { 0, 1, -1, 127, 128, -128, -129, 16383, 16384,
                               -16384, 2147483647LL, -2147483648LL, INT64_MAX, INT64_MIN };
    for (size_t i = 0; i < sizeof(bounds)/sizeof(bounds[0]); ++i) {
        bench::SimTick t = base;
        for (int k = 0; k < 5; ++k) { t.pxOff[k] = bounds[i]; t.vol[k] = bounds[i]; }
        t.lastOff = bounds[i]; t.volChg = bounds[i]; t.turnOff = bounds[i]; t.oiChg = bounds[i];
        char nm[64]; snprintf(nm, sizeof nm, "bound %lld", (long long)bounds[i]);
        One(t, nm);
    }

    if (fails) printf("\n%d FAILURES\n", fails);
    else       printf("\nALL PASS (fast == vector, byte-identical)\n");
    return fails ? 1 : 0;
}
