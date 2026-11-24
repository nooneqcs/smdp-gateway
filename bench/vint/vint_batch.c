// vint_batch.c —— 批量投机：8字节一批检测，全1字节则批量解（位提取，不落栈）
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define TSC_GHZ 3.0
#define BUFSZ   (16u << 20)
#define REP     5

static uint8_t buf[BUFSZ];

static double now_ns(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts);
    return ts.tv_sec*1e9+ts.tv_nsec;
}

int main(void)
{
    /* 运行时随机：95% 1字节 + 5% 2字节（与 vint_fast2 可比）*/
    uint64_t seed = 88172645463325252ULL;
    size_t off = 0; int vint_cnt = 0;
    while (off + 3 < BUFSZ) {
        seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
        if ((seed % 100) < 95) {
            buf[off++] = (uint8_t)(seed & 0x7F);
        } else {
            buf[off++] = (uint8_t)((seed & 0x7F) | 0x80);
            buf[off++] = (uint8_t)((seed >> 8) & 0x7F);
        }
        vint_cnt++;
    }
    printf("填充 %zu 字节，%d 个 VInt（95%%1字节/5%%2字节）\n\n", off, vint_cnt);

    volatile int64_t sink = 0;
    for (int r = 0; r < REP; ++r) {
        const uint8_t *p = buf, *end = buf + off;
        int64_t acc = 0; long cnt = 0;
        double t0 = now_ns();

        while (p + 8 <= end) {
            uint64_t x;
            memcpy(&x, p, 8);                          /* 一次 load 8 字节 */
            if ((x & 0x8080808080808080ULL) == 0) {   /* 8 个全是 1 字节 */
                #pragma GCC unroll 8
                for (int k = 0; k < 8; ++k) {
                    int64_t v = (int64_t)((x >> (8*k)) & 0x7F);  /* 位提取，不落栈 */
                    acc += (v >> 1) ^ -(v & 1);                  /* ZigZag */
                }
                p += 8; cnt += 8;
            } else {                                   /* 有 2 字节，标量解一个 */
                uint64_t zz = *p++;
                if (zz & 0x80) {
                    int sh = 7; uint8_t b; zz &= 0x7F;
                    do { b = *p++; zz |= (uint64_t)(b & 0x7F) << sh; sh += 7; } while (b & 0x80);
                }
                acc += (int64_t)((zz >> 1) ^ -(int64_t)(zz & 1));
                cnt++;
            }
        }
        while (p < end) {                              /* 尾部 */
            uint64_t zz = *p++;
            if (zz & 0x80) {
                int sh = 7; uint8_t b; zz &= 0x7F;
                do { b = *p++; zz |= (uint64_t)(b & 0x7F) << sh; sh += 7; } while (b & 0x80);
            }
            acc += (int64_t)((zz >> 1) ^ -(int64_t)(zz & 1));
            cnt++;
        }

        double t1 = now_ns();
        sink += acc;
        double per = (t1 - t0) / (double)cnt;
        printf("第%d轮: %.3f ns/VInt (~%.2f cycles @%.1fGHz)  VInt=%ld\n",
               r+1, per, per*TSC_GHZ, TSC_GHZ, cnt);
    }
    return 0;
}

