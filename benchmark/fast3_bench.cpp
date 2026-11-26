#include "tick2mirp.h"
#include "encode_fast3.h"
#include <cstdio>
#include <cstdlib>
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
static int correctness()
{
    int fails = 0;
    bench::SimTick base{};
    base.ino = 20; base.changeNo = 123456;
    for (int k = 0; k < 5; ++k) {
        base.mdType[k] = (char)(k & 1);
        base.pl[k] = 1 + k; base.pxOff[k] = 2000 * (k + 1); base.vol[k] = 100 * (k + 1);
    }
    base.lastOff = 0; base.volChg = 10; base.turnOff = 38000000; base.oiChg = -50;

    const int64_t bounds[] = { 0, 1, -1, 127, 128, -128, -129, 16383, 16384,
                               -16384, 2147483647LL, -2147483648LL, INT64_MAX, INT64_MIN };
    for (size_t i = 0; i < sizeof(bounds)/sizeof(bounds[0]); ++i) {
        bench::SimTick t = base;
        if (i) {
            for (int k = 0; k < 5; ++k) { t.pxOff[k] = bounds[i]; t.vol[k] = bounds[i]; }
            t.lastOff = bounds[i]; t.volChg = bounds[i]; t.turnOff = bounds[i]; t.oiChg = bounds[i];
        }
        std::vector<uint8_t> slow; slow.reserve(256); bench::EncodeBody(slow, t);
        uint8_t buf[512];
        size_t n2 = fastenc3::EncodeBodyFast(buf, t);
        if (!(n2 == slow.size() && std::memcmp(buf, slow.data(), n2) == 0)) {
            ++fails; printf("FAIL body %zu fast=%zu slow=%zu\n", i, n2, slow.size());
        }
        std::vector<uint8_t> full = bench::EncodePacket(t, 7);
        size_t m = fastenc3::EncodePacketFast(buf, t, 7);
        if (!(m == full.size() && std::memcmp(buf, full.data(), m) == 0)) {
            ++fails; printf("FAIL full %zu fast=%zu slow=%zu\n", i, m, full.size());
        }
    }
    if (fails) printf("correctness: %d FAILURES\n", fails);
    else       printf("correctness: ALL PASS (byte-identical)\n");
    return fails;
}
int main(int argc, char** argv)
{
    if (correctness()) return 1;
    const size_t n = 4096;
    const size_t N = argc > 1 ? strtoull(argv[1], nullptr, 10) : 2000000;
    std::vector<bench::SimTick> ticks; fill_ticks(ticks, n);
    uint8_t buf[512];

    for (int round = 0; round < 3; ++round) {
        uint64_t t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            size_t len = fastenc3::EncodeBodyFast(buf, ticks[i % n]);
            g_sink += len + buf[0];
        }
        uint64_t t1 = now_ns();
        printf("[f3 body] %.2f ns/pkt  %.1f Mpps\n",
               (double)(t1 - t0) / N, (double)N / (t1 - t0) * 1000.0);

        t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            size_t len = fastenc3::EncodePacketFast(buf, ticks[i % n], (int32_t)i);
            g_sink += len + buf[0];
        }
        t1 = now_ns();
        printf("[f3 full] %.2f ns/pkt  %.1f Mpps\n",
               (double)(t1 - t0) / N, (double)N / (t1 - t0) * 1000.0);

        t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            const bench::SimTick& t = ticks[i % n];
            uint8_t* q = buf;
            q = fastenc3::PutVInt(q, t.ino);
            q = fastenc3::PutVInt(q, t.changeNo);
            for (int k = 0; k < 5; ++k) {
                q = fastenc3::PutVInt(q, t.pl[k]);
                q = fastenc3::PutVInt(q, t.pxOff[k]);
                q = fastenc3::PutVInt(q, t.vol[k]);
            }
            q = fastenc3::PutVInt(q, t.lastOff);
            q = fastenc3::PutVInt(q, t.volChg);
            q = fastenc3::PutVInt(q, t.turnOff);
            q = fastenc3::PutVInt(q, t.oiChg);
            g_sink += (uint64_t)(q - buf);
        }
        t1 = now_ns();
        printf("[VInt x19] %.2f ns/pkt\n", (double)(t1 - t0) / N);
    }
    printf("sink=%llu\n", (unsigned long long)g_sink);
    return 0;
}
