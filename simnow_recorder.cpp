#include <cstdint>
#include <x86intrin.h>
#include "ThostFtdcMdApi.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <x86intrin.h>
#include <string>
#include <vector>

static FILE* g_fp = nullptr;
static long  g_count = 0;
static bool  g_nowrite = false;
static FILE* g_ts_fp = nullptr;
static uint64_t g_cyc_sum = 0, g_cyc_min = ~0ull, g_cyc_max = 0;
static int   g_reqId = 0;

struct RecTick {
    char    instrument[16];
    int64_t exchangeTsUs;      // 当日 HH:MM:SS.mmm -> us
    double  last, bid1, ask1, preSettle;
    int64_t bid1Vol, ask1Vol;
    int64_t volume;
    double  turnover, openInterest;
};

class MdSpi : public CThostFtdcMdSpi {
public:
    explicit MdSpi(CThostFtdcMdApi* api) : m_api(api) {}

    void OnFrontConnected() override {
        printf("[inf] front connected\n"); fflush(stdout);
        CThostFtdcReqUserLoginField req{};
        strcpy(req.BrokerID, "9999");
        strcpy(req.UserID, m_user.c_str());
        strcpy(req.Password, m_pass.c_str());
        m_api->ReqUserLogin(&req, ++g_reqId);
    }
    void OnFrontDisconnected(int reason) override {
        printf("[err] front disconnected, reason=%d\n", reason); fflush(stdout);
    }
    void OnRspUserLogin(CThostFtdcRspUserLoginField* p,
                        CThostFtdcRspInfoField* info, int, bool) override {
        if (info && info->ErrorID != 0) {
            printf("[err] login: %d %s\n", info->ErrorID, info->ErrorMsg); fflush(stdout);
            return;
        }
        printf("[inf] login ok, TradingDay=%s\n", p ? p->TradingDay : "?"); fflush(stdout);
        if (getenv("NOSUB")) { printf("[inf] NOSUB: skip subscribe\n"); fflush(stdout); return; }
        std::vector<char*> arr;
        for (auto& s : m_subs) arr.push_back(const_cast<char*>(s.c_str()));
        if (arr.empty()) { printf("[err] no instruments\n"); return; }
        int r = m_api->SubscribeMarketData(arr.data(), (int)arr.size());
        printf("[inf] subscribe %zu, ret=%d\n", arr.size(), r); fflush(stdout);
    }
    void OnRspSubMarketData(CThostFtdcSpecificInstrumentField* f,
                            CThostFtdcRspInfoField* info, int, bool) override {
        if (info && info->ErrorID != 0)
            printf("[err] sub %s: %d %s\n", f ? f->InstrumentID : "?", info->ErrorID, info->ErrorMsg);
        else
            printf("[inf] sub ok: %s\n", f ? f->InstrumentID : "?");
        fflush(stdout);
    }
    void OnRtnDepthMarketData(CThostFtdcDepthMarketDataField* d) override {
        if (!d) return;
        uint64_t __t0 = __rdtsc();
        { struct timespec __rt; clock_gettime(CLOCK_REALTIME, &__rt);
          int64_t __ns = (int64_t)__rt.tv_sec * 1000000000LL + __rt.tv_nsec;
          int64_t __seq = g_count + 1;
          if (g_ts_fp) { fwrite(&__seq, 8, 1, g_ts_fp); fwrite(&__ns, 8, 1, g_ts_fp); } }
        RecTick t{};
        strncpy(t.instrument, d->InstrumentID, 15);
        int hh = 0, mm = 0, ss = 0;
        const char* ts = d->UpdateTime;
        hh = (ts[0]-48)*10 + (ts[1]-48);
        mm = (ts[3]-48)*10 + (ts[4]-48);
        ss = (ts[6]-48)*10 + (ts[7]-48);
        t.exchangeTsUs = ((int64_t)hh * 3600 + mm * 60 + ss) * 1000000LL
                       + (int64_t)d->UpdateMillisec * 1000;
        t.last = d->LastPrice;
        t.bid1 = d->BidPrice1;
        t.ask1 = d->AskPrice1;
        t.bid1Vol = d->BidVolume1;
        t.ask1Vol = d->AskVolume1;
        t.preSettle = d->PreSettlementPrice;
        t.volume = d->Volume;
        t.turnover = d->Turnover;
        t.openInterest = d->OpenInterest;
        if (!g_nowrite) fwrite(&t, sizeof t, 1, g_fp);
        { uint64_t __d = __rdtsc() - __t0; g_cyc_sum += __d; if (__d < g_cyc_min) g_cyc_min = __d; if (__d > g_cyc_max) g_cyc_max = __d; }
        if (++g_count % 200 == 0) { fflush(g_fp); printf("[inf] %ld ticks\n", g_count); fflush(stdout); }
    }

    static std::string m_user, m_pass;
    std::vector<std::string> m_subs;

private:
    CThostFtdcMdApi* m_api;
};
std::string MdSpi::m_user, MdSpi::m_pass;

int main(int argc, char** argv)
{
    if (argc < 3) {
        printf("usage: %s <user> <pass> [front] [seconds] [inst...]\n", argv[0]);
        printf("  default front   = tcp://182.254.243.31:30012\n");
        printf("  default seconds = 60\n");
        return 1;
    }
    MdSpi::m_user = argv[1];
    MdSpi::m_pass = argv[2];
    const char* front = argc > 3 ? argv[3] : "tcp://182.254.243.31:30012";
    int seconds = argc > 4 ? atoi(argv[4]) : 60;

    g_nowrite = (getenv("NOWRITE") != nullptr);
    g_ts_fp = fopen("tick_ts.bin", "wb");
    g_fp = fopen("ticks_simnow.bin", "wb");
    if (!g_fp) { perror("fopen"); return 1; }

    CThostFtdcMdApi* api = CThostFtdcMdApi::CreateFtdcMdApi("./flow/");
    MdSpi spi(api);

    if (argc > 5) {
        for (int i = 5; i < argc; ++i) spi.m_subs.push_back(argv[i]);
    } else {
        spi.m_subs = { "au2602", "ag2602", "cu2601", "rb2605", "ni2601" };
    }

    api->RegisterSpi(&spi);
    api->RegisterFront(const_cast<char*>(front));
    api->Init();

    time_t end = time(nullptr) + seconds;
    while (time(nullptr) < end) sleep(1);

    if (g_count) printf("[cyc] handler avg=%.1f min=%llu max=%llu\n", (double)g_cyc_sum / g_count, (unsigned long long)g_cyc_min, (unsigned long long)g_cyc_max);
    printf("[inf] total %ld ticks -> ticks_simnow.bin\n", g_count);
    fclose(g_fp);
    if (g_ts_fp) fclose(g_ts_fp);
    api->Release();
    return 0;
}
