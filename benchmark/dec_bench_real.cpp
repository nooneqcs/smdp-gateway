#include "encode.h"
#include "market2.h"
#include <cstdio>
#include <cstdint>
#include <ctime>
#include <vector>
#include <random>
using namespace smdp;

static double now_ns(){ timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1e9+ts.tv_nsec; }

// 一条真实感增量报文：contracts 个合约域串
static std::vector<uint8_t> gen_packet(std::mt19937& rng, int contracts) {
    std::vector<uint8_t> body;
    for (int c = 0; c < contracts; ++c) {
        int64_t ino = rng() % 1000;
        EncodeContractHead(body, ContractDeltaHead{ino, (int64_t)(rng() % 100000)});
        int levels = 1 + rng() % 2;                     // 1-2 个分价表变动
        for (int l = 0; l < levels; ++l) {
            char ev = (char)('1' + rng() % 3);          // 1/2/3
            char md = (char)('0' + rng() % 2);          // 0/1
            int64_t lvl = 1 + rng() % 5;
            int64_t off = (int64_t)(rng() % 200) - 100; // ±100 tick → 1-2B
            int64_t vol = rng() % 5000;                 // 1-2B
            EncodePriceLevelDelta(body, PriceLevelDelta{ev, md, lvl, off, vol});
        }
        int64_t lp = (int64_t)(rng() % 200) - 100;
        int64_t vc = (int64_t)(rng() % 3000);
        int64_t to = (int64_t)(rng() % 10000000);       // 成交额偏移 → 2-4B
        int64_t oc = (int64_t)(rng() % 1000) - 500;
        EncodeTradeSummary(body, TradeSummary{lp, vc, to, oc});
    }
    return PackMirpPacket(0x01, 1, 1001, 1, 1700000000, 11700, 0, body);
}

int main() {
    std::mt19937 rng(12345);
    const int K = 256;                                  // 256 条不同报文轮流解析
    std::vector<std::vector<uint8_t>> pkts(K);
    size_t total_bytes = 0;
    for (auto& p : pkts) { p = gen_packet(rng, 4); total_bytes += p.size(); }

    MarketStore store; MarketVisitor mv(store);
    MirpHeader h;
    const size_t N = 5000000;

    for (size_t i = 0; i < 100000; ++i) { auto& p = pkts[i % K]; ParseMirpHeader(p.data(), p.size(), h); IterateDomainsT(p.data()+24, h.Length, mv); }
    double t0 = now_ns();
    for (size_t i = 0; i < N; ++i) { auto& p = pkts[i % K]; ParseMirpHeader(p.data(), p.size(), h); IterateDomainsT(p.data()+24, h.Length, mv); }
    double t1 = now_ns();

    printf("[real-load] %.2f ns/pkt  %.1f Mpps  (avg %.0f B/pkt, %d contracts)\n",
           (t1 - t0) / N, N / (t1 - t0) * 1000.0,
           (double)total_bytes / K, 4);
    return 0;
}

