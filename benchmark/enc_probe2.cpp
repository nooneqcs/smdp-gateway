#include "tick2mirp.h"
#include <cstdio>
#include <ctime>
#include <vector>

static volatile uint64_t g_sink = 0;
static inline uint64_t now_ns() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main()
{
    const size_t N = 2000000;
    std::vector<uint8_t> out; out.reserve(256);

    smdp::ContractDeltaHead h; h.InstrumentNo = 20; h.ChangeNo = 123456;
    bench::SimTick t{};
    t.ino = 20; t.changeNo = 42;
    for (int k = 0; k < 5; ++k) {
        t.pl[k] = 1 + k; t.pxOff[k] = 2000 * (k + 1);
        t.vol[k] = 100; t.mdType[k] = (char)(k & 1);
    }

    for (int r = 0; r < 3; ++r) {
        uint64_t t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            out.clear();
            for (int k = 0; k < 77; ++k) out.push_back((uint8_t)k);
            g_sink += out.size();
        }
        uint64_t t1 = now_ns();
        printf("[77 push_back] %.2f ns/op\n", (double)(t1 - t0) / N);

        t0 = now_ns();
        for (size_t i = 0; i < N; ++i) { out.clear(); smdp::EncodeContractHead(out, h); g_sink += out.size(); }
        t1 = now_ns();
        printf("[1 domain    ] %.2f ns/op\n", (double)(t1 - t0) / N);

        t0 = now_ns();
        for (size_t i = 0; i < N; ++i) { out.clear(); smdp::PutVInt(out, (int64_t)i); g_sink += out.size(); }
        t1 = now_ns();
        printf("[1 PutVInt   ] %.2f ns/op\n", (double)(t1 - t0) / N);

        t0 = now_ns();
        for (size_t i = 0; i < N; ++i) { out.clear(); bench::EncodeBody(out, t); g_sink += out.size(); }
        t1 = now_ns();
        printf("[full body   ] %.2f ns/op\n", (double)(t1 - t0) / N);
    }
    printf("sink=%llu\n", (unsigned long long)g_sink);
    return 0;
}
