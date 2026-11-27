#include "tick2mirp.h"
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
static void fill(std::vector<bench::SimTick>& v, size_t n)
{
    Rng r(20261126); v.resize(n);
    for (size_t i = 0; i < n; ++i) {
        bench::SimTick t{};
        t.ino = 20; t.changeNo = (int64_t)i + 1;
        t.volChg = r.ri(1, 50); t.turnOff = (int64_t)t.volChg * 3800000; t.oiChg = r.ri(-200, 200);
        for (int k = 0; k < 5; ++k) {
            t.mdType[k] = (char)(k & 1); t.pl[k] = 1 + k;
            t.pxOff[k] = (int64_t)(k + 1) * 2000 + r.ri(-200, 200);
            t.vol[k] = r.ri(1, 500);
        }
        v[i] = t;
    }
}
// 定长版：19 个 int32，域头 4B，无 VInt、无分支
static inline size_t EncodeFixed(uint8_t* out, const bench::SimTick& t)
{
    uint8_t* p = out;
    *(uint16_t*)p = 0x0003; *(uint16_t*)(p + 2) = 8; p += 4;
    *(int32_t*)p = (int32_t)t.ino;      p += 4;
    *(int32_t*)p = (int32_t)t.changeNo; p += 4;
    for (int i = 0; i < 5; ++i) {
        *(uint16_t*)p = 0x1001; *(uint16_t*)(p + 2) = 14; p += 4;
        *p++ = (uint8_t)t.ev[i];
        *p++ = (uint8_t)t.mdType[i];
        *(int32_t*)p = (int32_t)t.pl[i];     p += 4;
        *(int32_t*)p = (int32_t)t.pxOff[i];  p += 4;
        *(int32_t*)p = (int32_t)t.vol[i];    p += 4;
    }
    *(uint16_t*)p = 0x1002; *(uint16_t*)(p + 2) = 16; p += 4;
    *(int32_t*)p = (int32_t)t.lastOff; p += 4;
    *(int32_t*)p = (int32_t)t.volChg;  p += 4;
    *(int32_t*)p = (int32_t)t.turnOff; p += 4;
    *(int32_t*)p = (int32_t)t.oiChg;   p += 4;
    return (size_t)(p - out);
}
int main(int argc, char** argv)
{
    const size_t n = 4096;
    const size_t N = argc > 1 ? strtoull(argv[1], nullptr, 10) : 2000000;
    std::vector<bench::SimTick> v; fill(v, n);
    uint8_t buf[512];
    printf("fixed-size body = %zu bytes (VInt body = 77)\n", EncodeFixed(buf, v[0]));
    for (int r = 0; r < 3; ++r) {
        uint64_t t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            size_t len = EncodeFixed(buf, v[i % n]);
            g_sink += len + buf[0];
        }
        uint64_t t1 = now_ns();
        printf("[fixed32] %.2f ns/pkt  %.1f Mpps\n",
               (double)(t1 - t0) / N, (double)N / (t1 - t0) * 1000.0);
    }
    printf("sink=%llu\n", (unsigned long long)g_sink);
    return 0;
}
