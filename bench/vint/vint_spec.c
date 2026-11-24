// vint_spec.c —— 投机顺序解码：假设连续 UNROLL 个 VInt 都是 1 字节，按定长展开；失配则回滚标量重解
// 数据生成与 vint_fast2.c 完全一致（95% 1字节 / 5% 2字节，运行时随机，防常量折叠）
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <x86intrin.h>

#define GHZ        3.0
#define NBYTES     16777213
#define ROUNDS     5
#define PAD        64

static uint64_t rng_state;
static inline uint32_t rnd(void){
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 7;
    rng_state ^= rng_state << 17;
    return (uint32_t)(rng_state >> 11);
}

static size_t fill(uint8_t* buf, size_t cap){
    size_t off = 0;
    while (off + 2 < cap){
        uint32_t seed = rnd();
        if ((seed % 100) < 95){
            buf[off++] = (uint8_t)(seed & 0x7F);            // 1 字节，最高位 0
        } else {
            buf[off++] = (uint8_t)((seed & 0x7F) | 0x80);   // 首字节，最高位 1
            buf[off++] = (uint8_t)((seed >> 8) & 0x7F);     // 次字节
        }
    }
    return off;
}

static inline int64_t zz(uint32_t v){
    return (int64_t)(v >> 1) ^ -(int64_t)(v & 1);
}

// 标量：变长，地址依赖 load→len→p
static inline const uint8_t* dec_scalar(const uint8_t* p, int64_t* acc){
    uint32_t b0 = *p;
    uint32_t v;
    if (b0 & 0x80){
        v = (b0 & 0x7F) | ((uint32_t)p[1] << 7);
        p += 2;
    } else {
        v = b0;
        p += 1;
    }
    *acc += zz(v);
    return p;
}

static int64_t run_scalar(const uint8_t* p, const uint8_t* end, uint64_t* nv){
    int64_t acc = 0; uint64_t n = 0;
    while (p < end){ p = dec_scalar(p, &acc); n++; }
    *nv = n; return acc;
}

// 投机 UNROLL=4
static int64_t run_spec4(const uint8_t* p, const uint8_t* end, uint64_t* nv){
    int64_t acc = 0; uint64_t n = 0;
    while (p + 4 <= end){
        uint32_t x; memcpy(&x, p, 4);
        if ((x & 0x80808080u) == 0){                    // 4 个全是 1 字节：投机成立
            acc += zz(p[0]) + zz(p[1]) + zz(p[2]) + zz(p[3]);
            p += 4; n += 4;
        } else {                                        // 失配：回滚，标量重解这 4 个
            for (int i = 0; i < 4; i++){ p = dec_scalar(p, &acc); n++; }
        }
    }
    while (p < end){ p = dec_scalar(p, &acc); n++; }
    *nv = n; return acc;
}

// 投机 UNROLL=8
static int64_t run_spec8(const uint8_t* p, const uint8_t* end, uint64_t* nv){
    int64_t acc = 0; uint64_t n = 0;
    while (p + 8 <= end){
        uint64_t x; memcpy(&x, p, 8);
        if ((x & 0x8080808080808080ULL) == 0){          // 8 个全是 1 字节
            acc += zz(p[0]) + zz(p[1]) + zz(p[2]) + zz(p[3])
                 + zz(p[4]) + zz(p[5]) + zz(p[6]) + zz(p[7]);
            p += 8; n += 8;
        } else {
            for (int i = 0; i < 8; i++){ p = dec_scalar(p, &acc); n++; }
        }
    }
    while (p < end){ p = dec_scalar(p, &acc); n++; }
    *nv = n; return acc;
}

typedef int64_t (*fn_t)(const uint8_t*, const uint8_t*, uint64_t*);

static void bench(const char* name, fn_t fn, const uint8_t* buf, const uint8_t* end){
    printf("--- %s ---\n", name);
    for (int r = 1; r <= ROUNDS; r++){
        int64_t acc; uint64_t nv;
        uint64_t t0 = __rdtsc();
        acc = fn(buf, end, &nv);
        uint64_t t1 = __rdtsc();
        uint64_t cyc = t1 - t0;
        printf("第%d轮: %.3f ns/VInt (%.2f cycles @%.1fGHz) VInt=%lu\n",
               r, (double)cyc / GHZ / (double)nv, (double)cyc / (double)nv, GHZ, nv);
        (void)acc;
    }
}

int main(void){
    rng_state = __rdtsc() | 1;
    uint8_t* buf = (uint8_t*)malloc(NBYTES + PAD);
    if (!buf){ perror("malloc"); return 1; }
    size_t off = fill(buf, NBYTES);
    memset(buf + off, 0, PAD);
    const uint8_t* end = buf + off;
    printf("填充 %zu 字节，约 %lu 个 VInt（95%% 1字节 / 5%% 2字节）\n",
           off, (unsigned long)(off * 100 / 105));

    // 正确性：三种解法结果必须完全一致
    int64_t a1, a2, a3; uint64_t n1, n2, n3;
    a1 = run_scalar(buf, end, &n1);
    a2 = run_spec4 (buf, end, &n2);
    a3 = run_spec8 (buf, end, &n3);
    if (a1 != a2 || a1 != a3 || n1 != n2 || n1 != n3){
        printf("!!! 结果不一致: scalar(%lld,%lu) spec4(%lld,%lu) spec8(%lld,%lu)\n",
               (long long)a1, n1, (long long)a2, n2, (long long)a3, n3);
        return 2;
    }
    printf("正确性校验通过：acc=%lld VInt=%lu\n\n", (long long)a1, n1);

    bench("scalar (基准, 应复现 4.48)", run_scalar, buf, end);
    bench("spec4  (投机4)", run_spec4, buf, end);
    bench("spec8  (投机8)", run_spec8, buf, end);
    free(buf);
    return 0;
}

