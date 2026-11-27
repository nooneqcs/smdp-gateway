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
    int64_t  update_time, turnover;
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
    std::memcpy(d.instrument, s.instrument, 8); d.instrument[7] = 0;
    d.update_time = s.update_time; d.turnover = s.turnover;
    d.last_price = to_fixed(s.last_price, inv);
    d.bid_price  = to_fixed(s.bid_price,  inv);
    d.ask_price  = to_fixed(s.ask_price,  inv);
    d.ref_price  = to_fixed(s.ref_price,  inv);
    d.open_interest = (int32_t)std::llround(s.open_interest);
    d.bid_volume = (int32_t)s.bid_volume; d.ask_volume = (int32_t)s.ask_volume;
    d.volume = (int32_t)s.volume; d.trading_day = 20231114;
    d.update_millisec = 0; d.exchange_id = 0; d.flags = 0;
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
    FILE* f = fopen(path, "rb"); if (!f) { perror(path); return 1; }
    std::vector<RecTickWire> recs; RecTickWire r;
    while (fread(&r, sizeof r, 1, f) == 1) recs.push_back(r);
    fclose(f);
    const size_t n = recs.size(); if (!n) return 1;

    const double tick = 0.02, inv = 1.0 / tick;
    qgw::QuoteSnapshot<RecTick64> snap;
    double ghz = tsc_ghz();

    const double   target_tps  = 1.5e6;                        // 真实行情 1.5M/s
    const uint64_t TICK_CYCLES = (uint64_t)(ghz * 1e9 / target_tps);  // 每 tick 间隔
    const uint64_t SPIN        = (uint64_t)(ghz * 1e3);        // 策略每笔 ~1µs

    std::atomic<bool>     stop{false};
    std::atomic<uint64_t> pub_total{0}, reads{0}, distinct{0}, skipped{0}, bad{0};

    std::thread prod([&]{
        size_t i = 0; uint64_t seq = 0, next = __rdtsc();
        while (!stop.load(std::memory_order_relaxed)) {
            RecTick64 t = compress(recs[i], inv);
            t.update_time = (int64_t)seq++;
            snap.publish(t);
            if (++i == n) i = 0;
            next += TICK_CYCLES;
            while (__rdtsc() < next) {}          // 限速到 1.5M/s
        }
        pub_total.store(seq, std::memory_order_relaxed);
    });

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
                uint64_t t0 = __rdtsc(); while (__rdtsc() - t0 < SPIN) {}   // 策略 ~1µs
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop = true; prod.join(); strat.join();

    uint64_t P = pub_total.load(), R = reads.load(), D = distinct.load(), S = skipped.load(), B = bad.load();
    printf("published = %llu ticks  (%.2f M/s, 限速 1.5M/s)\n", (unsigned long long)P, P / 2.0 / 1e6);
    printf("strategy  : reads=%llu distinct=%llu skipped=%llu bad=%llu\n",
           (unsigned long long)R, (unsigned long long)D, (unsigned long long)S, (unsigned long long)B);
    printf("coverage  : %llu/%llu = %.1f%%\n",
           (unsigned long long)D, (unsigned long long)P, P ? 100.0 * D / P : 0);
    printf("verdict   : %s\n", B ? "FAIL(torn)" : "PASS(no tear)");
    return 0;
}

