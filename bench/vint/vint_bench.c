// vint_bench.c —— 上期所 SMDP 1档：解析官方示例报文(验证) + 测 VInt 解码 cycles
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define TSC_GHZ 3.0

/* 官方示例：报单增量 40 字节（3.1.1）*/
static const uint8_t msg_order[40] = {
    0x01,0x01,0x10,0x00,0x02,0x00,0x00,0x00,0xE9,0x03,0x00,0x00,0x02,0x00,0x00,0x00,
    0x77,0x8B,0x0D,0x4F,0xB4,0x2D,0x00,0x00,0x03,0x00,0x02,0x00,0x00,0x04,0x01,0x10,
    0x06,0x00,0x31,0x30,0x02,0x00,0x02,0x00
};
/* 官方示例：成交增量 73 字节（3.1.3）*/
static const uint8_t msg_trade[73] = {
    0x01,0x01,0x31,0x00,0x04,0x00,0x00,0x00,0xE9,0x03,0x00,0x00,0x04,0x00,0x00,0x00,
    0x81,0x8B,0x0D,0x4F,0xB4,0x2D,0x00,0x00,0x03,0x00,0x02,0x00,0x00,0x08,0x01,0x10,
    0x06,0x00,0x33,0x30,0x02,0x00,0x04,0x00,0x01,0x10,0x06,0x00,0x31,0x31,0x02,0x00,
    0x02,0x00,0x02,0x10,0x04,0x00,0x00,0x08,0x00,0x08,0x13,0x10,0x01,0x00,0x00,0x11,
    0x10,0x01,0x00,0x00,0x12,0x10,0x01,0x00,0x00
};

/* Varint + ZigZag 解码 */
static inline int64_t decode_vint(const uint8_t **pp)
{
    const uint8_t *p = *pp;
    uint64_t zz = 0;
    int shift = 0;
    uint8_t b;
    do {
        b = *p++;
        zz |= (uint64_t)(b & 0x7F) << shift;
        shift += 7;
    } while (b & 0x80);
    *pp = p;
    return (int64_t)((zz >> 1) ^ (-(int64_t)(zz & 1)));
}

/* 解析一条 MIRP 报文：打印每个域，校验字段（1档示例） */
static int parse_mirp(const uint8_t *buf)
{
    const uint8_t *p = buf + 24;                        /* 跳过 24 字节 MIRP 头 */
    int16_t len = *(const int16_t *)(buf + 2);          /* Length（小端）*/
    const uint8_t *end = p + len;
    int nvint = 0;

    while (p < end) {
        int16_t fid = *(const int16_t *)p; p += 2;      /* FieldID 小端 */
        int16_t fsz = *(const int16_t *)p; p += 2;      /* FieldSize 小端 */
        const uint8_t *fe = p + fsz;

        if (fid == 0x0003) {                            /* 合约增量头域 */
            int64_t inst = decode_vint(&p); nvint++;
            int64_t chg  = decode_vint(&p); nvint++;
            printf("  [头域] InstrumentNo=%lld ChangeNo=%lld\n",(long long)inst,(long long)chg);
        } else if (fid == 0x1001) {                     /* 分价表变动域 */
            char et = *p++, mt = *p++;
            int64_t lv = decode_vint(&p); nvint++;
            int64_t po = decode_vint(&p); nvint++;
            int64_t vo = decode_vint(&p); nvint++;
            printf("  [分价] %c %c Level=%lld Off=%lld Vol=%lld\n",et,mt,(long long)lv,(long long)po,(long long)vo);
        } else if (fid == 0x1002) {                     /* 成交概要域 */
            int64_t a = decode_vint(&p); nvint++;
            int64_t b = decode_vint(&p); nvint++;
            int64_t c = decode_vint(&p); nvint++;
            int64_t d = decode_vint(&p); nvint++;
            printf("  [成交] LastOff=%lld VolChg=%lld TurnOff=%lld OIChg=%lld\n",
                   (long long)a,(long long)b,(long long)c,(long long)d);
        } else {                                        /* 其他价格域：1 个 VInt */
            int64_t x = decode_vint(&p); nvint++;
            printf("  [域0x%04x] Off=%lld\n", fid, (long long)x);
        }
        p = fe;                                          /* 用 FieldSize 推进，丢弃冗余字节 */
    }
    return nvint;
}

/* 真实示例里的 VInt 字节序列（全 1 字节）：报单 5 + 成交 15 = 20 个 */
static const uint8_t vint_seq[] = {
    0x00,0x04,0x02,0x00,0x02,                         /* 报单 5 */
    0x00,0x08,0x02,0x00,0x04,0x02,0x00,0x02,0x00,0x08,0x00,0x08,0x00,0x00,0x00 /* 成交 15 */
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
    printf("=== 解析官方示例（验证正确性）===\n");
    printf("[报单 40B]\n"); int n1 = parse_mirp(msg_order);
    printf("[成交 73B]\n"); int n2 = parse_mirp(msg_trade);
    printf("报单 VInt=%d，成交 VInt=%d\n\n", n1, n2);

    printf("=== 测 VInt 解码（每 VInt cycles）===\n");
    printf("序列: %d 个真实 VInt（全 1 字节）\n", SEQ_N);
    volatile int64_t sink = 0;
    for (int r = 0; r < REP; ++r) {
        int64_t acc = 0;
        const uint8_t *p = vint_seq;
        double t0 = now_ns();
        for (int i = 0; i < ITERS; ++i) {
            for (int k = 0; k < SEQ_N; ++k) acc += decode_vint(&p);
            p = vint_seq;                                 /* 循环使用同一序列 */
        }
        double t1 = now_ns();
        sink += acc;
        double per = (t1 - t0) / (double)(ITERS * SEQ_N);
        printf("第%d轮: %.3f ns/VInt (~%.2f cycles @%.1fGHz)  校验=%lld\n",
               r+1, per, per*TSC_GHZ, TSC_GHZ, (long long)(acc & 1));
    }
    return 0;
}

