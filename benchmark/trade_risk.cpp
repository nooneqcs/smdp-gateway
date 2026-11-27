#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>

enum RiskResult { OK, REJ_LOTS, REJ_POS, REJ_CAGE, REJ_SELFTRADE, REJ_RATE };
static const char* rname(RiskResult r){ static const char* n[]={"OK","REJ_LOTS","REJ_POS","REJ_CAGE","REJ_SELFTRADE","REJ_RATE"}; return n[r]; }

struct RiskCfg   { int64_t maxLotsPerOrder=100, maxPosition=1000; double cagePct=0.10; int maxPerSec=50; };
struct RiskState { int64_t pos=0; uint64_t winStart=0; int winCount=0; };

static RiskResult check(const RiskCfg& c, RiskState& s, bool buy, double price, int64_t vol,
                        double last, double myBid1, double myAsk1, uint64_t now){
    if(vol<=0 || vol>c.maxLotsPerOrder) return REJ_LOTS;
    if(std::llabs(s.pos + (buy?vol:-vol)) > c.maxPosition) return REJ_POS;
    if(last>0 && std::fabs(price-last)/last > c.cagePct) return REJ_CAGE;
    if(buy  && myAsk1>0 && price>=myAsk1) return REJ_SELFTRADE;   // 买价打到自己的卖一
    if(!buy && myBid1>0 && price<=myBid1) return REJ_SELFTRADE;   // 卖价打到自己的买一
    if(now - s.winStart >= 1000000000ULL){ s.winStart=now; s.winCount=0; }
    if(s.winCount >= c.maxPerSec) return REJ_RATE;
    return OK;
}
static void commit(RiskState& s, bool buy, int64_t vol, uint64_t now){
    s.pos += (buy? vol : -vol);
    if(now - s.winStart >= 1000000000ULL){ s.winStart=now; s.winCount=0; }
    ++s.winCount;
}

static int g_fail=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %d %s\n",__LINE__,#c); ++g_fail; } }while(0)

int main(){
    RiskCfg c; RiskState s;
    CHECK(check(c,s,true,100,10,100,0,0,0)==OK);                       // 正常
    CHECK(check(c,s,true,100,200,100,0,0,0)==REJ_LOTS);                // 超单笔量
    s.pos=990; CHECK(check(c,s,true,100,20,100,0,0,0)==REJ_POS); s.pos=0;   // 超持仓
    CHECK(check(c,s,true,120,10,100,0,0,0)==REJ_CAGE);                 // 价格笼子(120 vs 100 = 20%>10%)
    CHECK(check(c,s,true,100,10,100,0,99,0)==REJ_SELFTRADE);           // 买价≥自己卖一
    CHECK(check(c,s,false,100,10,100,101,0,0)==REJ_SELFTRADE);         // 卖价≤自己买一

    RiskState r;                                                        // 速率：50 笔后第 51 拒
    for(int i=0;i<50;i++){ CHECK(check(c,r,true,100,1,100,0,0,0)==OK); commit(r,true,1,0); }
    CHECK(check(c,r,true,100,1,100,0,0,0)==REJ_RATE);
    // 一秒后窗口重置，恢复
    CHECK(check(c,r,true,100,1,100,0,0,1000000001ULL)==OK);

    printf(g_fail?"RISK FAILED:%d\n":"RISK ALL PASS\n", g_fail);
    return g_fail?1:0;
}
