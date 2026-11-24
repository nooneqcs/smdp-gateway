// vint_mask.c —— 掩码批量解码（起点掩码 + pext）对照 fast2 原版
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <x86intrin.h>

#define GHZ     3.0
#define NBYTES  16777213
#define ROUNDS  5
#define PAD     64
#define HIMASK  0x8080808080808080ULL

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
static inline int64_t zz32(uint32_t v){ return (int64_t)(v >> 1) ^ -(int64_t)(v & 1); }

static inline __attribute__((always_inline))
int64_t ref_loop(const uint8_t* p, const uint8_t* end, uint64_t* nv){
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

// 掩码批量：每 8 字节算一次起点掩码；起点 = 前一字节 bit7 == 0
static inline __attribute__((always_inline))
int64_t mask_loop(const uint8_t* p, const uint8_t* end, uint64_t* nv){
    int64_t acc = 0; uint64_t cnt = 0;
    uint32_t pending = 0;                       // 1 → 本块首字节是上一块遗留的续字节
    while (p + 9 <= end){
        uint64_t x; memcpy(&x, p, 8);
        uint32_t b7     = (uint32_t)_pext_u64(x, HIMASK);   // 8 位：各字节 bit7
        uint32_t starts = (~((b7 << 1) | pending)) & 0xFFu;
        uint32_t s = starts;
        while (s){
            int i = __builtin_ctz(s);
            s &= s - 1;
            uint32_t v = (uint32_t)((x >> (i * 8)) & 0x7Fu);
            if (b7 & (1u << i)){
                uint32_t nb = (i < 7) ? (uint32_t)((x >> ((i + 1) * 8)) & 0x7Fu)
                                      : (uint32_t)(p[8] & 0x7Fu);
                v |= nb << 7;
            }
            acc += zz32(v);
            cnt++;
        }
        pending = (b7 >> 7) & 1u;
        p += 8;
    }
    while (p < end){
        uint32_t b0 = *p, v;
        if (b0 & 0x80){ v = (b0 & 0x7Fu) | ((uint32_t)(p[1] & 0x7Fu) << 7); p += 2; }
        else          { v = b0; p += 1; }
        acc += zz32(v); cnt++;
    }
    *nv = cnt; return acc;
}

int main(void){
    rng_state = __rdtsc() | 1;
    uint8_t* buf = (uint8_t*)malloc(NBYTES + PAD);
    if (!buf){ perror("malloc"); return 1; }
    size_t off = fill(buf, NBYTES);
    memset(buf + off, 0, PAD);
    const uint8_t* end = buf + off;
    printf("填充 %zu 字节，约 %lu 个 VInt（95%% 1字节 / 5%% 2字节）\n", off, (unsigned long)(off * 100 / 105));

    int64_t a1, a2; uint64_t n1, n2;
    a1 = ref_loop (buf, end, &n1);
    a2 = mask_loop(buf, end, &n2);
    if (a1 != a2 || n1 != n2){
        printf("!!! 不一致: ref(%lld,%lu) mask(%lld,%lu)\n", (long long)a1, n1, (long long)a2, n2);
        return 2;
    }
    printf("正确性校验通过：acc=%lld VInt=%lu\n\n", (long long)a1, n1);

    volatile int64_t sink;
    for (int i = 0; i < 2; i++){
        printf("--- %s ---\n", i ? "mask (掩码批量)" : "ref (应复现 4.48–4.59)");
        for (int r = 1; r <= ROUNDS; r++){
            int64_t acc; uint64_t nv;
            uint64_t t0 = __rdtsc();
            acc = i ? mask_loop(buf, end, &nv) : ref_loop(buf, end, &nv);
            uint64_t cyc = __rdtsc() - t0;
            printf("第%d轮: %.3f ns/VInt (%.2f cycles @%.1fGHz) VInt=%lu\n",
                   r, (double)cyc/GHZ/(double)nv, (double)cyc/(double)nv, GHZ, nv);
            sink = acc;
        }
    }
    (void)sink; free(buf); return 0;
}

