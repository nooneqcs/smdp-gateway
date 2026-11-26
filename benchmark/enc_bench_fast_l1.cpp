#include "tick2mirp.h"
#include "encode_fast.h"
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

static volatile uint64_t g_sink = 0;
static inline uint64_t now_ns() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
struct Rng {
    uint64_t s; explicit Rng(uint64_t seed) : s(seed) {}
    uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 2685821657736338717ull; }
    int ri(int lo, int hi) { return lo + int(next() % (uint64_t)(hi - lo + 1)); }
};
static void fill_ticks(std::vector<bench::SimTick>& v, size_t n)
{
    Rng r(20261126); v.resize(n);
    for (size_t i = 0; i < n; ++i) {
        bench::SimTick t{};
        t.ino = 20; t.changeNo = (int64_t)i + 1;
        t.volChg = r.ri(1, 50); t.turnOff = t.volChg * 3800000; t.oiChg = r.ri(-200, 200);
        for (int k = 0; k < 5; ++k) {
            t.mdType[k] = (char)(k & 1); t.pl[k] = 1 + k;
            t.pxOff[k] = (int64_t)(k + 1) * 2000 + r.ri(-200, 200);
            t.vol[k] = r.ri(1, 500);
        }
        v[i] = t;
    }
}
int main(int argc, char** argv)
{
    const size_t n = 64;
    const size_t N = argc > 1 ? strtoull(argv[1], nullptr, 10) : 2000000;
    std::vector<bench::SimTick> ticks; fill_ticks(ticks, n);

    uint8_t buf[512];
    for (int round = 0; round < 3; ++round) {
        uint64_t t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            size_t len = fastenc::EncodeBodyFast(buf, ticks[i % n]);
            g_sink += len + buf[0];
        }
        uint64_t t1 = now_ns();
        printf("[fast body] %.2f ns/pkt  %.1f Mpps\n",
               (double)(t1 - t0) / N, (double)N / (t1 - t0) * 1000.0);

        t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            size_t len = fastenc::EncodePacketFast(buf, ticks[i % n], (int32_t)i);
            g_sink += len + buf[0];
        }
        t1 = now_ns();
        printf("[fast full] %.2f ns/pkt  %.1f Mpps\n",
               (double)(t1 - t0) / N, (double)N / (t1 - t0) * 1000.0);
    }
    printf("sink=%llu\n", (unsigned long long)g_sink);
    return 0;
}
