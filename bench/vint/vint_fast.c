// vint_fast.c —— 极致版：展开 + 内联 + 1字节快路径 + 无写回
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define TSC_GHZ 3.0

static const uint8_t vint_seq[] = {
    0x00,0x04,0x02,0x00,0x02,
    0x00,0x08,0x02,0x00,0x04,0x02,0x00,0x02,0x00,0x08,0x00,0x08,0x00,0x00,0x00
};
#define SEQ_N (int)(sizeof(vint_seq)/sizeof(vint_seq[0]))

#define ITERS 1000000
#define REP   5

static double now_ns(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1e9+ts.tv_nsec;
}

int main(void)
{
    volatile int64_t sink = 0;
    printf("序列 %d 个 VInt（全1字节），迭代 %d 轮（每轮 %d×%d）\n\n",
           SEQ_N, REP, ITERS, SEQ_N);

    for (int r = 0; r < REP; ++r) {
        const uint8_t *p = vint_seq;
        int64_t acc = 0;
        double t0 = now_ns();

        for (int i = 0; i < ITERS; ++i) {
            /* 展开 20 个，手动内联：1 字节快路径 */
            #pragma GCC unroll 20
            for (int k = 0; k < SEQ_N; ++k) {
                uint64_t zz = *p++;                       /* 1 字节快路径 */
                if (__builtin_expect(zz & 0x80, 0)) {      /* 99% 不进入 */
                    int shift = 7;
                    uint8_t b;
                    zz &= 0x7F;
                    do {
                        b = *p++;
                        zz |= (uint64_t)(b & 0x7F) << shift;
                        shift += 7;
                    } while (b & 0x80);
                }
                acc += (int64_t)((zz >> 1) ^ (-(int64_t)(zz & 1)));  /* ZigZag */
            }
            p = vint_seq;                                  /* 重置，不写回指针 */
        }

        double t1 = now_ns();
        sink += acc;
        double per = (t1 - t0) / (double)(ITERS * SEQ_N);
        printf("第%d轮: %.3f ns/VInt (~%.2f cycles @%.1fGHz)  校验=%lld\n",
               r+1, per, per*TSC_GHZ, TSC_GHZ, (long long)(acc & 1));
    }
    return 0;
}

