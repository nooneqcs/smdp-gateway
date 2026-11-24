// vint_mask8.c —— 无 pending 版 + 尾部续字节修正（mask7 的修正版）
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <x86intrin.h>

#define GHZ     3.0
#define NBYTES  16777213
#define ROUNDS  5
#define PAD     64

static uint8_t shuf[256][16];
static void init_table(void){
    for (int m = 0; m < 256; m++){
        int o = 0;
        for (int i = 0; i < 8; i++)
            if (m >> i & 1){ shuf[m][o++] = (uint8_t)(2*i); shuf[m][o++] = (uint8_t)(2*i+1); }
        while (o < 16) shuf[m][o++] = 0x80;
    }
}
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

#define BLOCK8(ACCV, CNT) do {                                                     \
    __m128i _x  = _mm_loadu_si128((const __m128i*)(p - 1));                        \
    uint32_t _mk = (uint32_t)_mm_movemask_epi8(_x) & 0xFFu;                        \
    uint32_t _st = (~_mk) & 0xFFu;                                                 \
    __m128i _a  = _mm_cvtepu8_epi16(_mm_srli_si128(_x, 1));                        \
    __m128i _b  = _mm_cvtepu8_epi16(_mm_srli_si128(_x, 2));                        \
    __m128i _lo = _mm_and_si128(_a, M7F);                                          \
    __m128i _hi = _mm_slli_epi16(_mm_and_si128(_b, M7F), 7);                       \
    __m128i _m2 = _mm_cmpeq_epi16(_mm_and_si128(_a, M80), M80);                    \
    __m128i _v  = _mm_or_si128(_lo, _mm_and_si128(_hi, _m2));                      \
    __m128i _vc = _mm_shuffle_epi8(_v, _mm_loadu_si128((const __m128i*)shuf[_st]));\
    __m128i _z  = _mm_xor_si128(_mm_srli_epi16(_vc, 1),                            \
                                _mm_sub_epi16(_mm_setzero_si128(), _mm_and_si128(_vc, ONE))); \
    ACCV = _mm_add_epi32(ACCV, _mm_madd_epi16(_z, ONE));                           \
    CNT += __builtin_popcount(_st);                                                \
    p += 8;                                                                        \
} while (0)

static inline __attribute__((always_inline))
int32_t reduce32(__m128i v){
    v = _mm_hadd_epi32(v, v); v = _mm_hadd_epi32(v, v);
    return _mm_cvtsi128_si32(v);
}

static inline __attribute__((always_inline))
int64_t mask8(const uint8_t* buf, const uint8_t* end, uint64_t* nv){
    const __m128i M7F = _mm_set1_epi16(0x7F);
    const __m128i M80 = _mm_set1_epi16(0x80);
    const __m128i ONE = _mm_set1_epi16(1);
    const uint8_t* p = buf;
    __m128i v0 = _mm_setzero_si128(), v1 = _mm_setzero_si128();
    int64_t acc64 = 0; uint64_t cnt = 0; int it = 0;

    while (p + 40 <= end){
        BLOCK8(v0, cnt); BLOCK8(v1, cnt); BLOCK8(v0, cnt); BLOCK8(v1, cnt);
        if (++it == 512){ acc64 += reduce32(v0) + reduce32(v1); v0 = v1 = _mm_setzero_si128(); it = 0; }
    }
    while (p + 16 <= end){
        BLOCK8(v0, cnt);
        if (++it == 512){ acc64 += reduce32(v0); v0 = _mm_setzero_si128(); it = 0; }
    }
    acc64 += reduce32(v0) + reduce32(v1);

    if (p > buf && (p[-1] & 0x80)) p += 1;   // 最后一块末尾若是 2 字节起点，跳过其续字节

    while (p < end){
        uint32_t b0 = *p, v;
        if (b0 & 0x80){ v = (b0 & 0x7Fu) | ((uint32_t)(p[1] & 0x7Fu) << 7); p += 2; }
        else          { v = b0; p += 1; }
        acc64 += (int64_t)((v >> 1) ^ -(int64_t)(v & 1)); cnt++;
    }
    *nv = cnt; return acc64;
}

int main(void){
    init_table();
    rng_state = __rdtsc() | 1;
    uint8_t* raw = (uint8_t*)malloc(NBYTES + PAD * 2);
    if (!raw){ perror("malloc"); return 1; }
    uint8_t* buf = raw + 16;
    buf[-1] = 0;                                   // pad：让第一个字节被判为起点
    size_t off = fill(buf, NBYTES);
    memset(buf + off, 0, PAD);
    const uint8_t* end = buf + off;
    printf("填充 %zu 字节，约 %lu 个 VInt（95%% 1字节 / 5%% 2字节）\n", off, (unsigned long)(off * 100 / 105));

    int64_t a1, a2; uint64_t n1, n2;
    a1 = ref_loop(buf, end, &n1);
    a2 = mask8(buf, end, &n2);
    if (a1 != a2 || n1 != n2){
        printf("!!! 不一致: ref(%lld,%llu) mask8(%lld,%llu)\n",
               (long long)a1, (unsigned long long)n1, (long long)a2, (unsigned long long)n2);
        return 2;
    }
    printf("正确性校验通过：acc=%lld VInt=%llu\n\n", (long long)a1, (unsigned long long)n1);

    volatile int64_t sink;
    for (int r = 1; r <= ROUNDS; r++){
        int64_t acc; uint64_t nv;
        uint64_t t0 = __rdtsc();
        acc = mask8(buf, end, &nv);
        uint64_t cyc = __rdtsc() - t0;
        printf("第%d轮: %.3f ns | %.2f ticks/VInt | %.3f ticks/byte | VInt=%llu\n",
               r, (double)cyc/GHZ/(double)nv, (double)cyc/(double)nv,
               (double)cyc/(double)off, (unsigned long long)nv);
        sink = acc;
    }
    (void)sink; free(raw); return 0;
}

