#include "quote_snapshot.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <cmath>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <x86intrin.h>

// ---------- 线格式（读 bin，必须 packed） ----------
#pragma pack(push,1)
struct RecTickWire {
    char     instrument[16];
    int64_t  update_time;
    double   last_price, bid_price, ask_price, ref_price;
    int64_t  bid_volume, ask_volume, volume, turnover;
    double   open_interest;
};
#pragma pack(pop)
static_assert(sizeof(RecTickWire) == 96, "wire must be 96B");

// ---------- 内存快照结构（自然对齐，手动排布，零 padding） ----------
struct alignas(64) RecTick64 {
    int64_t  update_time;      //  0
    int64_t  turnover;         //  8
    char     instrument[8];    // 16
    int32_t  last_price;       // 24
    int32_t  bid_price;        // 28
    int32_t  ask_price;        // 32
    int32_t  ref_price;        // 36
    int32_t  open_interest;    // 40
    int32_t  bid_volume;       // 44
    int32_t  ask_volume;       // 48
    int32_t  volume;           // 52
    int32_t  trading_day;      // 56
    int16_t  update_millisec;  // 60
    uint8_t  exchange_id;      // 62
    uint8_t  flags;            // 63
};
static_assert(sizeof(RecTick64) == 64, "tick64 must be 64B");
static_assert(offsetof(RecTick64, last_price)      == 24, "");
static_assert(offsetof(RecTick64, trading_day)     == 56, "");
static_assert(offsetof(RecTick64, update_millisec) == 60, "");

// ---------- 定点 / 压缩 ----------
static int32_t to_fixed(double p, double tick) {
    double k = std::llround(p / tick);
    if (k < -2147483648.0 || k > 2147483647.0) {
        std::fprintf(stderr, "FIXED OVERFLOW\n"); std::abort();
    }
    return (int32_t)k;
}
static RecTick64 compress(const RecTickWire& s, double tick) {
    RecTick64 d{};
    std::strncpy(d.instrument, s.instrument, 7);
    d.instrument[7] = 0;
    d.update_time = s.update_time;
    d.turnover    = s.turnover;
    d.last_price  = to_fixed(s.last_price, tick);
    d.bid_price   = to_fixed(s.bid_price,  tick);
    d.ask_price   = to_fixed(s.ask_price,  tick);
    d.ref_price   = to_fixed(s.ref_price,  tick);
    d.open_interest = (int32_t)std::llround(s.open_interest);
    d.bid_volume  = (int32_t)s.bid_volume;
    d.ask_volume  = (int32_t)s.ask_volume;
    d.volume      = (int32_t)s.volume;
    d.trading_day = 20231114;   // demo 占位
    d.update_millisec = 0;
    d.exchange_id = 0;
    d.flags = 0;
    return d;
}

static double tsc_ghz() {
    auto t0 = std::chrono::steady_clock::now(); uint64_t c0 = __rdtsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    uint64_t c1 = __rdtsc(); auto t1 = std::chrono::steady_clock::now();
    return (double)(c1 - c0) / std::chrono::duration<double>(t1 - t0).count() / 1e9;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "ticks_simnow.bin";
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    std::vector<RecTickWire> recs; RecTickWire r;
    while (fread(&r, sizeof r, 1, f) == 1) recs.push_back(r);
    fclose(f);
    const size_t n = recs.size();
    printf("loaded %zu records, wire=%zuB tick64=%zuB\n", n, sizeof(RecTickWire), sizeof(RecTick64));
    if (!n) return 1;

    const double tick = 0.02;
    RecTick64 s0 = compress(recs[0], tick);
    printf("sample: %.*s last=%d(%.2f) bid=%d ask=%d vol=%d turnover=%lld\n",
           (int)strlen(s0.instrument), s0.instrument, s0.last_price,
           s0.last_price * tick, s0.bid_price, s0.ask_price, s0.volume,
           (long long)s0.turnover);
    if (s0.bid_price > s0.last_price || s0.last_price > s0.ask_price)
        printf("WARN price order\n");

    qgw::QuoteSnapshot<RecTick64> snap;
    double ghz = tsc_ghz();

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> rreads{0}, bad{0};
    std::thread w([&]{
        size_t i = 0;
        while (!stop.load(std::memory_order_relaxed))
            snap.publish(compress(recs[i++ % n], tick));
    });
    std::thread rr([&]{
        RecTick64 t;
        while (!stop.load(std::memory_order_relaxed))
            if (snap.latest(t)) {
                rreads.fetch_add(1, std::memory_order_relaxed);
                if (t.bid_price > t.ask_price) bad.fetch_add(1, std::memory_order_relaxed);
            }
    });
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop = true; w.join(); rr.join();
    printf("concurrent: reads=%llu order_violations=%llu -> %s\n",
           (unsigned long long)rreads.load(), (unsigned long long)bad.load(),
           bad.load() ? "FAIL" : "PASS");

    const int N = 5000000;
    RecTick64 t = compress(recs[0], tick), o;
    volatile int64_t sink = 0;
    uint64_t c0, c1;

    c0 = __rdtsc();
    for (int i = 0; i < N; ++i) { RecTick64 x = compress(recs[i % n], tick); sink += x.last_price; }
    c1 = __rdtsc();
    double ns_comp = (double)(c1 - c0) / N / ghz;

    c0 = __rdtsc(); for (int i = 0; i < N; ++i) snap.publish(t);
    c1 = __rdtsc(); double ns_pub = (double)(c1 - c0) / N / ghz;

    c0 = __rdtsc(); for (int i = 0; i < N; ++i) { snap.latest(o); sink += o.last_price; }
    c1 = __rdtsc(); double ns_lat = (double)(c1 - c0) / N / ghz;

    printf("compress=%.2f ns publish=%.2f ns latest=%.2f ns (TSC %.3f GHz)\n",
           ns_comp, ns_pub, ns_lat, ghz);
    return 0;
}

