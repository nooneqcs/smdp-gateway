#include "tick2mirp.h"
#include "encode_fast.h"
#include <cstdio>
#include <cstring>
#include <ctime>

static volatile uint64_t g_sink = 0;
static inline uint64_t now_ns() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

int main()
{
    const size_t N = 2000000;
    uint8_t buf[512];

    for (int r = 0; r < 3; ++r) {
        uint64_t t0, t1;

        t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            uint8_t* p = buf;
            for (int k = 0; k < 76; ++k) *p++ = (uint8_t)(k + i);
            g_sink += buf[0];
        }
        t1 = now_ns(); printf("[76 x 1B    ] %.2f ns\n", (double)(t1 - t0) / N);

        t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            uint16_t* p = (uint16_t*)buf;
            for (int k = 0; k < 38; ++k) p[k] = (uint16_t)(k + i);
            g_sink += buf[0];
        }
        t1 = now_ns(); printf("[38 x 2B    ] %.2f ns\n", (double)(t1 - t0) / N);

        t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            uint32_t* p = (uint32_t*)buf;
            for (int k = 0; k < 19; ++k) p[k] = (uint32_t)(k + i);
            g_sink += buf[0];
        }
        t1 = now_ns(); printf("[19 x 4B    ] %.2f ns\n", (double)(t1 - t0) / N);

        t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            uint8_t* p = buf;
            for (int k = 0; k < 19; ++k) p = fastenc::PutVInt(p, 300);
            g_sink += (uint64_t)(p - buf);
        }
        t1 = now_ns(); printf("[19 VInt 常量] %.2f ns\n", (double)(t1 - t0) / N);

        t0 = now_ns();
        for (size_t i = 0; i < N; ++i) {
            uint8_t* p = buf;
            p = fastenc::PutVInt(p, 20);
            p = fastenc::PutVInt(p, 123456);
            for (int k = 0; k < 5; ++k) {
                p = fastenc::PutVInt(p, 1 + k);
                p = fastenc::PutVInt(p, 2000 * (k + 1) + (int)(i % 200));
                p = fastenc::PutVInt(p, 1 + (int)(i % 500));
            }
            p = fastenc::PutVInt(p, 0);
            p = fastenc::PutVInt(p, 10 + (int)(i % 50));
            p = fastenc::PutVInt(p, (int64_t)(10 + i % 50) * 3800000);
            p = fastenc::PutVInt(p, (int)(i % 400) - 200);
            g_sink += (uint64_t)(p - buf);
        }
        t1 = now_ns(); printf("[19 VInt 真实] %.2f ns\n", (double)(t1 - t0) / N);
    }
    printf("sink=%llu\n", (unsigned long long)g_sink);
    return 0;
}
