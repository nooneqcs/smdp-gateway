// vint_mask12.c —— 2 块显式分层 + zigzag 用算术右移
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
int32_t reduce32(__m128i v){
    v = _mm_hadd_epi32(v, v); v = _mm_hadd_epi32(v, v);
    return _mm_cvtsi128_si32(v);
}

static inline __attribute__((always_inline))
int64_t mask12(const uint8_t* buf, const uint8_t* end, uint64_t* nv){
    const __m128i M7F  = _mm_set1_epi16(0x7F);
    const __m128i M80  = _mm_set1_epi16(0x80);
    const __m128i M128 = _mm_set1_epi16(128);
    const __m128i ONE  = _mm_set1_epi16(1);
    const __m128i ZERO = _mm_setzero_si128();
    const uint8_t* p = buf;
    __m128i m2prev = ZERO;
    __m128i acc0 = ZERO, acc1 = ZERO;
    int64_t acc64 = 0; uint64_t cnt = 0; int it = 0;

    while (p + 24 <= end){
        // 阶段 1：两次 load 与转换、m2 全部先算
        __m128i A0 = _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i*)(p)));
        __m128i B0 = _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i*)(p + 1)));
        __m128i A1 = _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i*)(p + 8)));
        __m128i B1 = _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i*)(p + 9)));
        __m128i M0 = _mm_cmpeq_epi16(_mm_and_si128(A0, M80), M80);
        __m128i M1 = _mm_cmpeq_epi16(_mm_and_si128(A1, M80), M80);

        // 阶段 2：carry 链
        __m128i P0 = _mm_alignr_epi8(M0, m2prev, 14);
        __m128i P1 = _mm_alignr_epi8(M1, M0, 14);
        m2prev = M1;
        __m128i S0 = _mm_cmpeq_epi16(P0, ZERO);
        __m128i S1 = _mm_cmpeq_epi16(P1, ZERO);

        // 阶段 3：数值
        __m128i V0 = _mm_and_si128(_mm_or_si128(_mm_and_si128(A0, M7F),
                         _mm_and_si128(_mm_mullo_epi16(_mm_and_si128(B0, M7F), M128), M0)), S0);
        __m128i V1 = _mm_and_si128(_mm_or_si128(_mm_and_si128(A1, M7F),
                         _mm_and_si128(_mm_mullo_epi16(_mm_and_si128(B1, M7F), M128), M1)), S1);

        // zigzag：-(v&1) 用 slli(15)+srai(15) 得到
        __m128i Z0 = _mm_xor_si128(_mm_srli_epi16(V0, 1), _mm_srai_epi16(_mm_slli_epi16(V0, 15), 15));
        __m128i Z1 = _mm_xor_si128(_mm_srli_epi16(V1, 1), _mm_srai_epi16(_mm_slli_epi16(V1, 15), 15));

        acc0 = _mm_add_epi32(acc0, _mm_madd_epi16(Z0, ONE));
        acc1 = _mm_add_epi32(acc1, _mm_madd_epi16(Z1, ONE));
        cnt += (uint64_t)__builtin_popcount((uint32_t)_mm_movemask_epi8(S0)) / 2u
             + (uint64_t)__builtin_popcount((uint32_t)_mm_movemask_epi8(S1)) / 2u;
        p += 16;
        if (++it == 1024){ acc64 += reduce32(acc0) + reduce32(acc1); acc0 = acc1 = ZERO; it = 0; }
    }
    // 剩余 8 字节块走单块逻辑
    while (p + 16 <= end){
        __m128i A0 = _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i*)(p)));
        __m128i B0 = _mm_cvtepu8_epi16(_mm_loadl_epi64((const __m128i*)(p + 1)));
        __m128i M0 = _mm_cmpeq_epi16(_mm_and_si128(A0, M80), M80);
        __m128i P0 = _mm_alignr_epi8(M0, m2prev, 14);
        __m128i S0 = _mm_cmpeq_epi16(P0, ZERO);
        m2prev = M0;
        __m128i V0 = _mm_and_si128(_mm_or_si128(_mm_and_si128(A0, M7F),
                         _mm_and_si128(_mm_mullo_epi16(_mm_and_si128(B0, M7F), M128), M0)), S0);
        __m128i Z0 = _mm_xor_si128(_mm_srli_epi16(V0, 1), _mm_srai_epi16(_mm_slli_epi16(V0, 15), 15));
        acc0 = _mm_add_epi32(acc0, _mm_madd_epi16(Z0, ONE));
        cnt += (uint64_t)__builtin_popcount((uint32_t)_mm_movemask_epi8(S0)) / 2u;
        p += 8;
        if (++it == 1024){ acc64 += reduce32(acc0); acc0 = ZERO; it = 0; }
    }
    acc64 += reduce32(acc0) + reduce32(acc1);

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
    const uint8_t* end = buf + off;
    printf("填充 %zu 字节，约 %lu 个 VInt（95%% 1字节 / 5%% 2字节）\n", off, (unsigned long)(off * 100 / 105));

    int64_t a1, a2; uint64_t n1, n2;
    a1 = ref_loop(buf, end, &n1);
    a2 = mask12(buf, end, &n2);
    if (a1 != a2 || n1 != n2){
        printf("!!! 不一致: ref(%lld,%llu) mask12(%lld,%llu)\n",
               (long long)a1, (unsigned long long)n1, (long long)a2, (unsigned long long)n2);
        return 2;
    }
    printf("正确性校验通过：acc=%lld VInt=%llu\n\n", (long long)a1, (unsigned long long)n1);

    volatile int64_t sink;
    for (int r = 1; r <= ROUNDS; r++){
        int64_t acc; uint64_t nv;
        uint64_t t0 = __rdtsc();
        acc = mask12(buf, end, &nv);
        uint64_t cyc = __rdtsc() - t0;
        printf("第%d轮: %.3f ns | %.2f ticks/VInt | %.3f ticks/byte | VInt=%llu\n",
               r, (double)cyc/GHZ/(double)nv, (double)cyc/(double)nv,
               (double)cyc/(double)off, (unsigned long long)nv);
        sink = acc;
    }
    (void)sink; free(raw); return 0;
}

