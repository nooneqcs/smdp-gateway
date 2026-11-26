#include "tick2mirp_c.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

static volatile uint64_t g_sink = 0;
static inline uint64_t now_ns() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
struct Rng {
    uint64_t s; explicit Rng(uint64_t x) : s(x) {}
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
    int ri(int a, int b) { return a + int(next() % (uint64_t)(b - a + 1)); }
};

int main()
{
    const size_t n = 4096;
    const size_t N = 2000000;

    std::vector<benchc::SimTickC> v(n);
    Rng r(20261126);
    for (size_t i = 0; i < n; ++i) {
        benchc::SimTickC t{};
        t.ino = 20; t.changeNo = (uint32_t)(i + 1);
        t.volChg = r.ri(1, 50); t.turnOff = (int64_t)t.volChg * 3800000; t.oiChg = r.ri(-200, 200);
        for (int k = 0; k < 5; ++k) {
            t.mdType[k] = (uint8_t)(k & 1); t.pl[k] = (int8_t)(1 + k);
            t.pxOff[k] = (int16_t)(2000 * (k + 1) + r.ri(-200, 200));
            t.vol[k] = (int16_t)r.ri(1, 500);
        }
        v[i] = t;
    }
    printf("sizeof(SimTickC)=%zu, working set=%zu KB\n",
           sizeof(benchc::SimTickC), n * sizeof(benchc::SimTickC) / 1024);

    uint8_t buf[256];
    for (int round = 0; round < 3; ++round) {
        uint64_t t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            std::memcpy(buf, &v[i % n], sizeof(benchc::SimTickC));
            asm volatile("" :: "r"(buf) : "memory");
            g_sink += buf[0];
        }
        uint64_t t1 = now_ns();
        printf("[memcpy 64B] %.2f ns/pkt  %.1f Mpps\n",
               (double)(t1 - t0) / N, (double)N / (t1 - t0) * 1000.0);
    }
    printf("sink=%llu\n", (unsigned long long)g_sink);
    return 0;
}
