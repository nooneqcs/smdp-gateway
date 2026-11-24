// vint_fast2.c —— 运行时随机数据 + 1字节快路径，防常量折叠
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define TSC_GHZ 3.0
#define BUFSZ   (16u << 20)     /* 16MB */
#define ITERS   1
#define REP     5

static uint8_t buf[BUFSZ];

static double now_ns(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1e9+ts.tv_nsec;
}

int main(void)
{
    /* 运行时随机填充：95% 1字节 VInt，5% 2字节（贴近真实"几乎全1字节"）*/
    uint64_t seed = 88172645463325252ULL;
    size_t off = 0;
    int vint_cnt = 0;
    while (off + 3 < BUFSZ) {
        seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
        if ((seed % 100) < 100) {
            buf[off++] = (uint8_t)(seed & 0x7F);           /* 1字节，最高位0 */
        } else {
            buf[off++] = (uint8_t)((seed & 0x7F) | 0x80);   /* 2字节第1字节，最高位1 */
            buf[off++] = (uint8_t)((seed >> 8) & 0x7F);     /* 2字节第2字节 */
        }
        vint_cnt++;
    }
    printf("填充 %zu 字节，约 %d 个 VInt（95%% 1字节 / 5%% 2字节）\n\n", off, vint_cnt);

    volatile int64_t sink = 0;
    for (int r = 0; r < REP; ++r) {
        const uint8_t *p = buf;
        const uint8_t *end = buf + off;
        int64_t acc = 0;
        int cnt = 0;
        double t0 = now_ns();

        while (p < end) {
            uint64_t zz = *p++;
            if (__builtin_expect(zz & 0x80, 0)) {
                int shift = 7;
                uint8_t b;
                zz &= 0x7F;
                do {
                    b = *p++;
                    zz |= (uint64_t)(b & 0x7F) << shift;
                    shift += 7;
                } while (b & 0x80);
            }
            acc += (int64_t)((zz >> 1) ^ (-(int64_t)(zz & 1)));
            cnt++;
        }

        double t1 = now_ns();
        sink += acc;
        double per = (t1 - t0) / (double)cnt;
        printf("第%d轮: %.3f ns/VInt (~%.2f cycles @%.1fGHz)  VInt=%d\n",
               r+1, per, per*TSC_GHZ, TSC_GHZ, cnt);
    }
    return 0;
}

