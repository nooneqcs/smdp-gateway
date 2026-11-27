#include "quote_snapshot.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <x86intrin.h>

// ---------- 两个候选 tick，只有大小不同 ----------
struct Tick96 { uint64_t id, check; uint64_t payload[10]; };
static_assert(sizeof(Tick96) == 96, "");
struct Tick64 { uint64_t id, check; uint64_t payload[6];  };
static_assert(sizeof(Tick64) == 64, "");

template <typename T> T makeTick(uint64_t id) {
    T t; t.id = id; t.check = id * 0x9E3779B97F4A7C15ULL;
    constexpr int NP = (sizeof(T) - 16) / 8;
    for (int i = 0; i < NP; ++i) t.payload[i] = id + i;
    return t;
}
template <typename T> bool okTick(const T& t) {
    if (t.check != t.id * 0x9E3779B97F4A7C15ULL) return false;
    constexpr int NP = (sizeof(T) - 16) / 8;
    for (int i = 0; i < NP; ++i) if (t.payload[i] != t.id + i) return false;
    return true;
}

static double tsc_ghz() {
    auto t0 = std::chrono::steady_clock::now(); uint64_t c0 = __rdtsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    uint64_t c1 = __rdtsc(); auto t1 = std::chrono::steady_clock::now();
    return (double)(c1 - c0) / std::chrono::duration<double>(t1 - t0).count() / 1e9;
}

template <typename T>
void bench(const char* name, qgw::QuoteSnapshot<T>& snap, double ghz) {
    // 1) 并发正确性
    std::atomic<bool>     stop{false};
    std::atomic<uint64_t> reads{0}, tears{0}, skips{0};
    std::thread w([&]{
        uint64_t id = 1;
        while (!stop.load(std::memory_order_relaxed)) snap.publish(makeTick<T>(id++));
    });
    std::thread r([&]{
        T t;
        while (!stop.load(std::memory_order_relaxed)) {
            if (snap.latest(t)) {
                reads.fetch_add(1, std::memory_order_relaxed);
                if (!okTick(t)) tears.fetch_add(1, std::memory_order_relaxed);
            } else skips.fetch_add(1, std::memory_order_relaxed);
        }
    });
    std::this_thread::sleep_for(std::chrono::seconds(2));
    stop = true; w.join(); r.join();
    printf("[%s] size=%zuB words=%zu reads=%llu tears=%llu skips=%llu -> %s\n",
        name, sizeof(T), qgw::QuoteSnapshot<T>::WORDS,
        (unsigned long long)reads.load(), (unsigned long long)tears.load(),
        (unsigned long long)skips.load(), tears.load() ? "FAIL" : "PASS");

    // 2) 延迟（无竞争，单线程）
    const int N = 5000000;
    T t = makeTick<T>(1), o;
    volatile uint64_t sink = 0;
    uint64_t c0 = __rdtsc();
    for (int i = 0; i < N; ++i) snap.publish(t);
    uint64_t c1 = __rdtsc();
    double pub = (double)(c1 - c0) / N / ghz;

    c0 = __rdtsc();
    for (int i = 0; i < N; ++i) { snap.latest(o); sink += o.id; }
    c1 = __rdtsc();
    double lat = (double)(c1 - c0) / N / ghz;
    printf("[%s] publish=%.2f ns  latest=%.2f ns\n", name, pub, lat);
}

int main() {
    double ghz = tsc_ghz();
    printf("TSC = %.3f GHz\n", ghz);

    qgw::QuoteSnapshot<Tick96> s96;
    qgw::QuoteSnapshot<Tick64> s64;
    bench<Tick96>("96B", s96, ghz);
    bench<Tick64>("64B", s64, ghz);

    const int N = 5000000; volatile uint64_t sink = 0;
    uint64_t c0 = __rdtsc();
    for (int i = 0; i < N; ++i) sink += i;
    uint64_t c1 = __rdtsc();
    printf("loop baseline avg = %.2f ns (未扣)\n", (double)(c1 - c0) / N / ghz);
    return 0;
}

