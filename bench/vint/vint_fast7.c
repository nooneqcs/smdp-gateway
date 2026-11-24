// vint_fast7.c —— 寄存器滑动窗口：4 VInt / 1 次 8 字节 load
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <x86intrin.h>

#define GHZ     3.0
#define NBYTES  16777213
#define ROUNDS  5
#define PAD     64

static uint64_t rng_state;
static inline uint32_t rnd(void){
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 7; rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 11);
}
static size_t fill(uint8_t* buf, size_t cap){
    size_t off = 0;
    while (off + 2 < cap){
        uint32_t seed = rnd();
        if ((seed % 100) < 95) buf[off++] = (uint8_t)(seed & 0x7F);
        else { buf[off++] = (uint8_t)((seed & 0x7F) | 0x80); buf[off++] = (uint8_t)((seed >> 8) & 0x7F); }
    }
    return off;
}
static int64_t ref_loop(const uint8_t* p, const uint8_t* end, uint64_t* nv){
    int64_t acc = 0; uint64_t cnt = 0;
    while (p < end){
        uint64_t zz = *p++;
        if (__builtin_expect(zz & 0x80, 0)){
            int shift = 7; uint8_t b; zz &= 0x7F;
            do { b = *p++; zz |= (uint64_t)(b & 0x7F) << shift; shift += 7; } while (b & 0x80);
        }
        acc += (int64_t)((zz >> 1) ^ (-(int64_t)(zz & 1)));
        cnt++;
    }
    *nv = cnt; return acc;
}

// 标量：逐字节从内存读（对照）
#define DECODE(ACC) do {                                                        \
    uint64_t zz = *p++;                                                         \
    if (__builtin_expect(zz & 0x80, 0)){                                        \
        int sh = 7; uint8_t b; zz &= 0x7F;                                      \
        do { b = *p++; zz |= (uint64_t)(b & 0x7F) << sh; sh += 7; } while (b & 0x80); \
    }                                                                           \
    ACC += (int64_t)((zz >> 1) ^ (-(int64_t)(zz & 1)));                         \
} while (0)

// 窗口：从 win 取，推进靠寄存器右移；consume 累计已吃字节
#define DECODE_W(ACC) do {                                                      \
    int _len = 1;                                                               \
    uint64_t _z = win & 0x7F;                                                   \
    if (__builtin_expect(win & 0x80, 0)){ _z |= ((win >> 8) & 0x7F) << 7; _len = 2; } \
    win >>= _len * 8;                                                           \
    ACC += (int64_t)((_z >> 1) ^ -(int64_t)(_z & 1));                           \
    consume += _len;                                                            \
} while (0)

int main(void){
    rng_state = __rdtsc() | 1;
    uint8_t* buf = (uint8_t*)malloc(NBYTES + PAD);
    if (!buf){ perror("malloc"); return 1; }
    size_t off = fill(buf, NBYTES);
    memset(buf + off, 0, PAD);
    const uint8_t* end = buf + off;
    printf("填充 %zu 字节，约 %lu 个 VInt（95%% 1字节 / 5%% 2字节）\n", off, (unsigned long)(off * 100 / 105));

    // 正确性
    int64_t a_ref; uint64_t n_ref;
    a_ref = ref_loop(buf, end, &n_ref);
    {
        const uint8_t* p = buf;
        uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0; uint64_t n = 0;
        while (p + 8 <= end){
            uint64_t win = 0; memcpy(&win, p, 8);
            int consume = 0;
            DECODE_W(a0); DECODE_W(a1); DECODE_W(a2); DECODE_W(a3);
            p += consume; n += 4;
        }
        while (p < end){ DECODE(a0); n++; }
        if ((int64_t)(a0 + a1 + a2 + a3) != a_ref || n != n_ref){
            printf("!!! 不一致: ref(%lld,%llu) fast7(%lld,%llu)\n",
                   (long long)a_ref, (unsigned long long)n_ref,
                   (long long)(a0+a1+a2+a3), (unsigned long long)n);
            return 2;
        }
        printf("正确性校验通过：acc=%lld VInt=%llu\n\n", (long long)a_ref, (unsigned long long)n_ref);
    }

    volatile int64_t sink;
    printf("--- ref (逐字节 load) ---\n");
    for (int r = 1; r <= ROUNDS; r++){
        const uint8_t* p = buf; int64_t acc = 0; uint64_t n = 0;
        uint64_t t0 = __rdtsc();
        while (p < end){ DECODE(acc); n++; }
        uint64_t cyc = __rdtsc() - t0;
        printf("第%d轮: %.3f ns | %.2f ticks/VInt | %.3f ticks/byte | VInt=%llu\n",
               r, (double)cyc/GHZ/(double)n, (double)cyc/(double)n,
               (double)cyc/(double)off, (unsigned long long)n);
        sink = acc;
    }

    printf("--- fast7 (寄存器窗口, 4 VInt/load) ---\n");
    for (int r = 1; r <= ROUNDS; r++){
        const uint8_t* p = buf;
        uint64_t a0 = 0, a1 = 0, a2 = 0, a3 = 0; uint64_t n = 0;
        uint64_t t0 = __rdtsc();
        while (p + 8 <= end){
            uint64_t win = 0; memcpy(&win, p, 8);
            int consume = 0;
            DECODE_W(a0); DECODE_W(a1); DECODE_W(a2); DECODE_W(a3);
            p += consume; n += 4;
        }
        while (p < end){ DECODE(a0); n++; }
        uint64_t cyc = __rdtsc() - t0;
        printf("第%d轮: %.3f ns | %.2f ticks/VInt | %.3f ticks/byte | VInt=%llu\n",
               r, (double)cyc/GHZ/(double)n, (double)cyc/(double)n,
               (double)cyc/(double)off, (unsigned long long)n);
        sink = a0 + a1 + a2 + a3;
    }
    (void)sink; free(buf); return 0;
}

