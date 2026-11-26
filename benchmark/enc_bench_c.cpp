#include <cstdlib>
#include "tick2mirp.h"
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

static void fill(std::vector<benchc::SimTickC>& v, size_t n)
{
    Rng r(20261126); v.resize(n);
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
}

// 与宽结构体版本逐字节比对
static int check()
{
    bench::SimTick a{};
    a.ino = 20; a.changeNo = 123456;
    for (int k = 0; k < 5; ++k) { a.mdType[k] = (char)(k & 1); a.pl[k] = 1 + k; a.pxOff[k] = 2000*(k+1); a.vol[k] = 100*(k+1); }
    a.lastOff = 0; a.volChg = 10; a.turnOff = 38000000; a.oiChg = -50;

    benchc::SimTickC b{};
    b.ino = 20; b.changeNo = 123456;
    for (int k = 0; k < 5; ++k) { b.mdType[k] = (uint8_t)(k & 1); b.pl[k] = (int8_t)(1+k); b.pxOff[k] = (int16_t)(2000*(k+1)); b.vol[k] = (int16_t)(100*(k+1)); }
    b.lastOff = 0; b.volChg = 10; b.turnOff = 38000000; b.oiChg = -50;

    uint8_t x[512], y[512];
    size_t nx = fastenc::EncodeBodyFast(x, a);
    size_t ny = benchc::EncodeBodyC(y, b);
    if (nx != ny || std::memcmp(x, y, nx) != 0) { printf("check FAIL %zu vs %zu\n", nx, ny); return 1; }
    printf("check OK (64B struct == 176B struct, %zu bytes)\n", nx);
    return 0;
}

int main(int argc, char** argv)
{
    if (check()) return 1;
    const size_t n = 4096;
    const size_t N = argc > 1 ? strtoull(argv[1], nullptr, 10) : 2000000;
    std::vector<benchc::SimTickC> v; fill(v, n);
    printf("sizeof(SimTickC)=%zu  working set=%zu KB\n", sizeof(benchc::SimTickC), n*sizeof(benchc::SimTickC)/1024);

    uint8_t buf[512];
    for (int r = 0; r < 3; ++r) {
        uint64_t t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            size_t len = benchc::EncodeBodyC(buf, v[i % n]);
            g_sink += len + buf[0];
        }
        uint64_t t1 = now_ns();
        printf("[64B body] %.2f ns/pkt  %.1f Mpps\n",
               (double)(t1 - t0) / N, (double)N / (t1 - t0) * 1000.0);
    }
    printf("sink=%llu\n", (unsigned long long)g_sink);
    return 0;
}
