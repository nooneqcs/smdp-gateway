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

#pragma pack(push,1)
struct RecTickWire {
    char     instrument[16];
    int64_t  update_time;
    double   last_price, bid_price, ask_price, ref_price;
    int64_t  bid_volume, ask_volume, volume, turnover;
    double   open_interest;
};
#pragma pack(pop)
static_assert(sizeof(RecTickWire) == 96, "");

struct alignas(64) RecTick64 {
    int64_t  update_time;
    int64_t  turnover;
    char     instrument[8];
    int32_t  last_price, bid_price, ask_price, ref_price;
    int32_t  open_interest;
    int32_t  bid_volume, ask_volume, volume;
    int32_t  trading_day;
    int16_t  update_millisec;
    uint8_t  exchange_id, flags;
};
static_assert(sizeof(RecTick64) == 64, "");

static inline int32_t to_fixed(double p, double inv) { return (int32_t)std::llround(p * inv); }
static RecTick64 compress(const RecTickWire& s, double inv) {
    RecTick64 d{};
    std::memcpy(d.instrument, s.instrument, 8);
    d.instrument[7] = 0;
    d.update_time = s.update_time;
    d.turnover    = s.turnover;
    d.last_price  = to_fixed(s.last_price, inv);
    d.bid_price   = to_fixed(s.bid_price,  inv);
    d.ask_price   = to_fixed(s.ask_price,  inv);
    d.ref_price   = to_fixed(s.ref_price,  inv);
    d.open_interest = (int32_t)std::llround(s.open_interest);
    d.bid_volume  = (int32_t)s.bid_volume;
    d.ask_volume  = (int32_t)s.ask_volume;
    d.volume      = (int32_t)s.volume;
    d.trading_day = 20231114;
    d.update_millisec = 0;
    d.exchange_id = 0;
    d.flags = 0;
    return d;
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "ticks_simnow.bin";
    FILE* f = fopen(path, "rb");
    if (!f) { perror(path); return 1; }
    std::vector<RecTickWire> recs; RecTickWire r;
    while (fread(&r, sizeof r, 1, f) == 1) recs.push_back(r);
    fclose(f);
    const size_t n = recs.size();
    if (!n) return 1;

    const double tick = 0.02, inv = 1.0 / tick;
    qgw::QuoteSnapshot<RecTick64> snap;

    std::atomic<bool>     stop{false};
    std::atomic<uint64_t> pub_total{0}, reads{0}, distinct{0}, skipped{0}, bad{0};
    const uint64_t SPIN = 600;                   // 模拟策略计算 ~200ns@3GHz

    // 行情核：compress -> publish，永不阻塞
    std::thread prod([&]{
        size_t i = 0; uint64_t seq = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            RecTick64 t = compress(recs[i], inv);
            t.update_time = (int64_t)(seq++);     // 借 update_time 当发布序号（骨架专用）
            snap.publish(t);
            if (++i == n) i = 0;
        }
        pub_total.store(seq, std::memory_order_relaxed);
    });

    // 策略核：只取最新，模拟有成本的计算
    std::thread strat([&]{
        RecTick64 t; uint64_t last = ~0ull;
        while (!stop.load(std::memory_order_relaxed)) {
            if (snap.latest(t)) {
                reads.fetch_add(1, std::memory_order_relaxed);
                uint64_t s = (uint64_t)t.update_time;
                if (last == ~0ull) { distinct.fetch_add(1, std::memory_order_relaxed); last = s; }
                else if (s > last) {
                    distinct.fetch_add(1, std::memory_order_relaxed);
                    if (s > last + 1) skipped.fetch_add(s - last - 1, std::memory_order_relaxed);
                    last = s;
                }
                if (t.bid_price > t.ask_price) bad.fetch_add(1, std::memory_order_relaxed);
                uint64_t t0 = __rdtsc(); while (__rdtsc() - t0 < SPIN) {}   // 模拟策略耗时
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop = true; prod.join(); strat.join();

    uint64_t P = pub_total.load(), R = reads.load(), D = distinct.load(), S = skipped.load(), B = bad.load();
    printf("published = %llu ticks  (%.2f M/s)\n", (unsigned long long)P, P / 2.0 / 1e6);
    printf("strategy  : reads=%llu distinct=%llu skipped=%llu bad=%llu\n",
           (unsigned long long)R, (unsigned long long)D, (unsigned long long)S, (unsigned long long)B);
    printf("coverage  : %llu/%llu = %.1f%%  (策略只处理最新，丢弃 %.1f%% 积压)\n",
           (unsigned long long)D, (unsigned long long)P,
           P ? 100.0 * D / P : 0, P ? 100.0 * (P - D) / P : 0);
    printf("verdict   : %s\n", B ? "FAIL(torn)" : "PASS(no tear)");
    return 0;
}

