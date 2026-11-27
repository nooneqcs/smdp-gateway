#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <map>

struct RecTick {
    char    instrument[16];
    int64_t exchangeTsUs;
    double  last, bid1, ask1, preSettle;
    int64_t bid1Vol, ask1Vol;
    int64_t volume;
    double  turnover, openInterest;
};

int main(int argc, char** argv)
{
    const char* path = argc > 1 ? argv[1] : "ticks_simnow.bin";
    FILE* f = fopen(path, "rb");
    if (!f) { perror("fopen"); return 1; }

    RecTick t;
    long n = 0;
    std::map<std::string, long> cnt;
    std::map<std::string, double> lo, hi, lastTs;
    long tsBack = 0, emptyBid = 0, zeroLast = 0;

    while (fread(&t, sizeof t, 1, f) == 1) {
        n++;
        std::string k(t.instrument);
        cnt[k]++;
        if (lo.find(k) == lo.end()) { lo[k] = hi[k] = t.last; lastTs[k] = t.exchangeTsUs; }
        else {
            if (t.last < lo[k]) lo[k] = t.last;
            if (t.last > hi[k]) hi[k] = t.last;
            if (t.exchangeTsUs < lastTs[k]) tsBack++;
            lastTs[k] = t.exchangeTsUs;
        }
        if (t.bid1 > 1e300) emptyBid++;
        if (t.last <= 0)    zeroLast++;
        if (n <= 8)
            printf("%-8s ts=%lld last=%.3f bid=%.3f ask=%.3f vol=%lld oi=%.0f\n",
                   t.instrument, (long long)t.exchangeTsUs, t.last, t.bid1, t.ask1,
                   (long long)t.volume, t.openInterest);
    }
    printf("\ntotal=%ld  file=%.1f KB\n", n, n * (double)sizeof(RecTick) / 1024.0);
    printf("ts_backwards=%ld  empty_bid(DBL_MAX)=%ld  zero_last=%ld\n", tsBack, emptyBid, zeroLast);
    for (auto& kv : cnt)
        printf("%-8s %6ld  last in [%.3f, %.3f]\n",
               kv.first.c_str(), kv.second, lo[kv.first], hi[kv.first]);
    return 0;
}
