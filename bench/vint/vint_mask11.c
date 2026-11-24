// vint_mask11.c —— 同一算法三档缓冲：32KB(L1) / 256KB(L2) / 16MB(流式)
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

#define BLOCK8(ACCV, CNT, M2PREV) do {                                             \
    __m128i _a  = _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i*)(p)));          \
    __m128i _b  = _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i*)(p + 1)));      \
    __m128i _m2 = _mm_cmpeq_epi16(_mm_and_si128(_a, M80), M80);                     \
    __m128i _pv = _mm_alignr_epi8(_m2, (M2PREV), 14);                              \
    __m128i _sl = _mm_cmpeq_epi16(_pv, ZERO);                                      \
    __m128i _lo = _mm_and_si128(_a, M7F);                                          \
    __m128i _hi = _mm_mullo_epi16(_mm_and_si128(_b, M7F), M128);                   \
    __m128i _v  = _mm_and_si128(_mm_or_si128(_lo, _mm_and_si128(_hi, _m2)), _sl);  \
    __m128i _z  = _mm_xor_si128(_mm_srli_epi16(_v, 1),                             \
                                _mm_sub_epi16(ZERO, _mm_and_si128(_v, ONE)));      \
    ACCV = _mm_add_epi32(ACCV, _mm_madd_epi16(_z, ONE));                           \
    CNT += (uint64_t)__builtin_popcount((uint32_t)_mm_movemask_epi8(_sl)) / 2u;    \
    (M2PREV) = _m2;                                                                \
    p += 8;                                                                        \
} while (0)

static inline __attribute__((always_inline))
int32_t reduce32(__m128i v){
    v = _mm_hadd_epi32(v, v); v = _mm_hadd_epi32(v, v);
    return _mm_cvtsi128_si32(v);
}

static inline __attribute__((always_inline))
int64_t mask11(const uint8_t* buf, const uint8_t* end, uint64_t* nv){
    const __m128i M7F  = _mm_set1_epi16(0x7F);
    const __m128i M80  = _mm_set1_epi16(0x80);
    const __m128i M128 = _mm_set1_epi16(128);
    const __m128i ONE  = _mm_set1_epi16(1);
    const __m128i ZERO = _mm_setzero_si128();
    const uint8_t* p = buf;
    __m128i m2prev = ZERO;
    __m128i v0 = ZERO, v1 = ZERO;
    int64_t acc64 = 0; uint64_t cnt = 0; int it = 0;

    while (p + 40 <= end){
        BLOCK8(v0, cnt, m2prev); BLOCK8(v1, cnt, m2prev);
        BLOCK8(v0, cnt, m2prev); BLOCK8(v1, cnt, m2prev);
        if (++it == 512){ acc64 += reduce32(v0) + reduce32(v1); v0 = v1 = ZERO; it = 0; }
    }
    while (p + 16 <= end){
        BLOCK8(v0, cnt, m2prev);
        if (++it == 512){ acc64 += reduce32(v0); v0 = ZERO; it = 0; }
    }
    acc64 += reduce32(v0) + reduce32(v1);

    if (p > buf && (p[-1] & 0x80)) p += 1;

    while (p < end){
        uint32_t b0 = *p, v;
        if (b0 & 0x80){ v = (b0 & 0x7Fu) | ((uint32_t)(p[1] & 0x7Fu) << 7); p += 2; }
        else          { v = b0; p += 1; }
        acc64 += (int64_t)((v >> 1) ^ -(int64_t)(v & 1)); cnt++;
    }
    *nv = cnt; return acc64;
}

int main(void){
    rng_state = __rdtsc() | 1;
    uint8_t* raw = (uint8_t*)malloc(NBYTES + PAD * 2);
    if (!raw){ perror("malloc"); return 1; }
    uint8_t* buf = raw + 16;
    buf[-1] = 0;
    size_t off = fill(buf, NBYTES);
    memset(buf + off, 0, PAD);
    printf("总填充 %zu 字节（95%% 1字节 / 5%% 2字节）\n\n", off);

    const size_t sizes[3] = { 32*1024, 256*1024, (size_t)NBYTES };
    const char*  name[3]  = { "32KB (L1 热)", "256KB (L2 热)", "16MB (流式)" };

    volatile int64_t sink;
    for (int s = 0; s < 3; s++){
        if (sizes[s] > off) continue;
        const uint8_t* end = buf + sizes[s];
        int64_t a1, a2; uint64_t n1, n2;
        a1 = ref_loop(buf, end, &n1);
        a2 = mask11(buf, end, &n2);
        if (a1 != a2 || n1 != n2){
            printf("!!! [%s] 不一致: ref(%lld,%llu) mask11(%lld,%llu)\n", name[s],
                   (long long)a1, (unsigned long long)n1, (long long)a2, (unsigned long long)n2);
            return 2;
        }
        printf("--- %s, VInt=%llu ---\n", name[s], (unsigned long long)n1);
        for (int r = 1; r <= ROUNDS; r++){
            int64_t acc; uint64_t nv;
            uint64_t t0 = __rdtsc();
            acc = mask11(buf, end, &nv);
            uint64_t cyc = __rdtsc() - t0;
            printf("第%d轮: %.3f ns | %.2f ticks/VInt | %.3f ticks/byte\n",
                   r, (double)cyc/GHZ/(double)nv, (double)cyc/(double)nv, (double)cyc/(double)sizes[s]);
            sink = acc;
        }
        printf("\n");
    }
    (void)sink; free(raw); return 0;
}

