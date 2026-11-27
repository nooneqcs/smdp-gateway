#include "quote_snapshot.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <x86intrin.h>

// 模拟 tick：96B，id + 派生校验字段，任何半更新都会被 ok() 抓到
struct Tick {
    uint64_t id;
    uint64_t check;
    uint64_t payload[10];
};
static_assert(sizeof(Tick) == 96, "");

static Tick make(uint64_t id) {
    Tick t; t.id = id;
    t.check = id * 0x9E3779B97F4A7C15ULL;
    for (int i = 0; i < 10; ++i) t.payload[i] = id + i;
    return t;
}
static bool ok(const Tick& t) {
    if (t.check != t.id * 0x9E3779B97F4A7C15ULL) return false;
    for (int i = 0; i < 10; ++i) if (t.payload[i] != t.id + i) return false;
    return true;
}

static qgw::QuoteSnapshot<Tick> g_snap;
static std::atomic<bool>     g_stop{false};
static std::atomic<uint64_t> g_reads{0}, g_tears{0}, g_skips{0};

static void writer() {
    uint64_t id = 1;
    while (!g_stop.load(std::memory_order_relaxed)) g_snap.publish(make(id++));
}
static void reader() {
    Tick t;
    while (!g_stop.load(std::memory_order_relaxed)) {
        if (g_snap.latest(t)) { ++g_reads; if (!ok(t)) ++g_tears; }
        else ++g_skips;
    }
}
static double tsc_ghz() {
    auto t0 = std::chrono::steady_clock::now(); uint64_t c0 = __rdtsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    uint64_t c1 = __rdtsc(); auto t1 = std::chrono::steady_clock::now();
    return (double)(c1 - c0) / std::chrono::duration<double>(t1 - t0).count() / 1e9;
}

int main() {
    printf("tick=%zuB  words/snapshot=%zu\n", sizeof(Tick), qgw::QuoteSnapshot<Tick>::WORDS);

    // 1) 并发正确性：满速写 + 并发读，2 秒，撕裂必须为 0
    std::thread w(writer), r(reader);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    g_stop = true; w.join(); r.join();
    printf("reads=%llu tears=%llu skips=%llu publishes=%llu\n",
        (unsigned long long)g_reads.load(), (unsigned long long)g_tears.load(),
        (unsigned long long)g_skips.load(), (unsigned long long)g_snap.publishes.load());
    if (g_tears.load()) { printf("FAIL: torn read!\n"); return 1; }
    printf("PASS: 0 tearing\n");

    // 2) 延迟（无竞争、单线程，未扣 rdtsc 基线，你自己按 25.9ns 那套扣）
    double ghz = tsc_ghz();
    auto ns = [&](double c) { return c / ghz; };
    const int N = 5000000;
    Tick t = make(1), o;
    volatile uint64_t sink = 0;

    uint64_t c0 = __rdtsc();
    for (int i = 0; i < N; ++i) g_snap.publish(t);
    uint64_t c1 = __rdtsc();
    printf("publish       avg = %.2f ns\n", ns((double)(c1 - c0) / N));

    c0 = __rdtsc();
    for (int i = 0; i < N; ++i) { g_snap.latest(o); sink += o.id; }
    c1 = __rdtsc();
    printf("latest (no cc) avg = %.2f ns\n", ns((double)(c1 - c0) / N));

    c0 = __rdtsc();
    for (int i = 0; i < N; ++i) sink += i;
    c1 = __rdtsc();
    printf("loop baseline   avg = %.2f ns (未扣)\n", ns((double)(c1 - c0) / N));
    return 0;
}

