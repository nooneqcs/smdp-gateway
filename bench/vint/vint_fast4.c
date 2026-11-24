// vint_fast4.c —— 无分支解码 vs vint_fast2 原版参考循环（同缓冲、同二进制对比）
// 数据：95% 1字节 / 5% 2字节，运行时随机（防常量折叠）
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
            buf[off++] = (uint8_t)(seed & 0x7F);
        } else {
            buf[off++] = (uint8_t)((seed & 0x7F) | 0x80);
            buf[off++] = (uint8_t)((seed >> 8) & 0x7F);
        }
    }
    return off;
}

// ---- 参考：vint_fast2 原版循环（先推进后判断，95% fallthrough，无 else）----
static int64_t run_ref(const uint8_t* p, const uint8_t* end, uint64_t* nv){
    int64_t acc = 0; uint64_t cnt = 0;
    while (p < end){
        uint64_t zz = *p++;
        if (__builtin_expect(zz & 0x80, 0)){
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
    *nv = cnt; return acc;
}

// ---- 无分支：无条件读 p[0]/p[1]（两 load 地址均已知，可并行），最长 2 字节 ----
static int64_t run_branchless(const uint8_t* p, const uint8_t* end, uint64_t* nv){
    int64_t acc = 0; uint64_t cnt = 0;
    while (p + 2 <= end){
        uint32_t b0 = p[0];
        uint32_t b1 = p[1];
        uint32_t hi   = b0 >> 7;                 // 0 或 1
        uint32_t mask = (uint32_t)0 - hi;        // 全 0 或全 1
        uint32_t v = (b0 & 0x7F) | (((b1 & 0x7F) << 7) & mask);
        acc += (int64_t)((v >> 1) ^ -(int64_t)(v & 1));
        p += 1 + hi;
        cnt++;
    }
    while (p < end){                             // 尾部
        uint32_t b0 = *p;
        uint32_t v;
        if (b0 & 0x80){ v = (b0 & 0x7F) | ((uint32_t)p[1] << 7); p += 2; }
        else          { v = b0; p += 1; }
        acc += (int64_t)((v >> 1) ^ -(int64_t)(v & 1));
        cnt++;
    }
    *nv = cnt; return acc;
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

    int64_t a1, a2; uint64_t n1, n2;
    a1 = run_ref(buf, end, &n1);
    a2 = run_branchless(buf, end, &n2);
    if (a1 != a2 || n1 != n2){
        printf("!!! 结果不一致: ref(%lld,%lu) branchless(%lld,%lu)\n",
               (long long)a1, n1, (long long)a2, n2);
        return 2;
    }
    printf("正确性校验通过：acc=%lld VInt=%lu\n\n", (long long)a1, n1);

    bench("ref (vint_fast2 原版, 应复现 4.48)", run_ref, buf, end);
    bench("branchless (无分支, 双 load)", run_branchless, buf, end);
    free(buf);
    return 0;
}

