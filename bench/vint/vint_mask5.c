// vint_mask5.c —— 掩码批量 v5：pdep 展开起点掩码，无表访问
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

static inline __attribute__((always_inline))
uint32_t block8(const uint8_t* p, uint32_t pending, __m128i* accv, uint64_t* cnt){
    const __m128i M7F = _mm_set1_epi16(0x7F);
    const __m128i M80 = _mm_set1_epi16(0x80);
    const __m128i ONE = _mm_set1_epi16(1);
    __m128i x = _mm_loadu_si128((const __m128i*)p);
    uint32_t b7 = (uint32_t)_mm_movemask_epi8(x) & 0xFFu;
    uint32_t starts = (~((b7 << 1) | pending)) & 0xFFu;

    // 起点掩码展开：8 个位 → 8 个 0/1 字节 → 8 个 0/0xFFFF 的 u16 lane
    uint64_t bits = _pdep_u64((uint64_t)starts, 0x0101010101010101ULL);
    __m128i bitv  = _mm_cvtepu8_epi16(_mm_cvtsi64_si128((long long)bits));
    __m128i lane  = _mm_sub_epi16(_mm_setzero_si128(), bitv);

    __m128i a  = _mm_cvtepu8_epi16(x);
    __m128i b  = _mm_cvtepu8_epi16(_mm_srli_si128(x, 1));
    __m128i lo = _mm_and_si128(a, M7F);
    __m128i hi = _mm_slli_epi16(_mm_and_si128(b, M7F), 7);
    __m128i m2 = _mm_cmpeq_epi16(_mm_and_si128(a, M80), M80);
    __m128i val = _mm_and_si128(_mm_or_si128(lo, _mm_and_si128(hi, m2)), lane);  // 非起点置 0

    __m128i z = _mm_xor_si128(_mm_srli_epi16(val, 1),
                              _mm_sub_epi16(_mm_setzero_si128(), _mm_and_si128(val, ONE)));
    *accv = _mm_add_epi32(*accv, _mm_madd_epi16(z, ONE));
    *cnt += __builtin_popcount(starts);
    return (b7 >> 7) & 1u;
}

static inline __attribute__((always_inline))
int32_t reduce32(__m128i v){
    v = _mm_hadd_epi32(v, v); v = _mm_hadd_epi32(v, v);
    return _mm_cvtsi128_si32(v);
}

static inline __attribute__((always_inline))
int64_t mask_loop(const uint8_t* buf, const uint8_t* end, uint64_t* nv){
    const uint8_t* p = buf; uint32_t pending = 0;
    int64_t acc64 = 0; uint64_t cnt = 0; int it = 0;
    __m128i a0 = _mm_setzero_si128(), a1 = _mm_setzero_si128();

    while (p + 40 <= end){
        uint32_t q0 = block8(p,      pending, &a0, &cnt);
        uint32_t q1 = block8(p + 8,  q0,      &a1, &cnt);
        uint32_t q2 = block8(p + 16, q1,      &a0, &cnt);
        uint32_t q3 = block8(p + 24, q2,      &a1, &cnt);
        pending = q3;
        p += 32;
        if (++it == 512){ acc64 += reduce32(a0) + reduce32(a1); a0 = a1 = _mm_setzero_si128(); it = 0; }
    }
    while (p + 16 <= end){
        pending = block8(p, pending, &a0, &cnt);
        p += 8;
        if (++it == 512){ acc64 += reduce32(a0); a0 = _mm_setzero_si128(); it = 0; }
    }
    acc64 += reduce32(a0) + reduce32(a1);

    if (pending) p += 1;
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
    uint8_t* buf = (uint8_t*)malloc(NBYTES + PAD);
    if (!buf){ perror("malloc"); return 1; }
    size_t off = fill(buf, NBYTES);
    memset(buf + off, 0, PAD);
    const uint8_t* end = buf + off;
    printf("填充 %zu 字节，约 %lu 个 VInt（95%% 1字节 / 5%% 2字节）\n", off, (unsigned long)(off * 100 / 105));

    int64_t a1, a2; uint64_t n1, n2;
    a1 = ref_loop(buf, end, &n1);
    a2 = mask_loop(buf, end, &n2);
    if (a1 != a2 || n1 != n2){
        printf("!!! 不一致: ref(%lld,%llu) mask5(%lld,%llu)\n",
               (long long)a1, (unsigned long long)n1, (long long)a2, (unsigned long long)n2);
        return 2;
    }
    printf("正确性校验通过：acc=%lld VInt=%llu\n\n", (long long)a1, (unsigned long long)n1);

    volatile int64_t sink;
    for (int r = 1; r <= ROUNDS; r++){
        int64_t acc; uint64_t nv;
        uint64_t t0 = __rdtsc();
        acc = mask_loop(buf, end, &nv);
        uint64_t cyc = __rdtsc() - t0;
        printf("第%d轮: %.3f ns/VInt | %.2f ticks/VInt | %.3f ticks/byte | VInt=%llu\n",
               r, (double)cyc / GHZ / (double)nv, (double)cyc / (double)nv,
               (double)cyc / (double)off, (unsigned long long)nv);
        sink = acc;
    }
    (void)sink; free(buf); return 0;
}

