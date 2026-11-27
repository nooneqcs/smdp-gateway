#include <cstdio>
#include <cstdint>
#include <cstring>

enum OrdStatus { ORD_INIT, ORD_SUBMITTED, ORD_ACCEPTED, ORD_PART, ORD_FILLED, ORD_CANCELED, ORD_REJECTED };
enum OrdEvent  { EV_ACCEPTED, EV_PARTIAL, EV_FILLED, EV_CANCELED, EV_REJECTED };

struct Order {
    uint64_t ref; char sysId[32]{}; OrdStatus st = ORD_INIT;
    int64_t volTotal = 0, volTraded = 0; double price = 0;
};
static inline bool terminal(const Order& o){ return o.st==ORD_FILLED||o.st==ORD_CANCELED||o.st==ORD_REJECTED; }

// OnRtnOrder 抽象事件
static void on_event(Order& o, OrdEvent ev, const char* sysId){
    if(sysId && *sysId && !o.sysId[0]) std::strncpy(o.sysId, sysId, 31);
    if(terminal(o)) return;                       // 幂等：终态不再变
    switch(ev){
        case EV_ACCEPTED: if(o.st==ORD_INIT||o.st==ORD_SUBMITTED) o.st=ORD_ACCEPTED; break; // 不把 PART 降回
        case EV_PARTIAL:  o.st=ORD_PART;   break;
        case EV_FILLED:   o.st=ORD_FILLED; break;
        case EV_CANCELED: o.st=ORD_CANCELED; break;
        case EV_REJECTED: o.st=ORD_REJECTED; break;
    }
}
// OnRtnTrade 成交明细（累计，幂等靠 volTraded 单调 + 终态停）
static void on_trade(Order& o, int64_t vol){
    if(terminal(o) || vol<=0) return;
    o.volTraded += vol;
    if(o.volTraded >= o.volTotal) o.st = ORD_FILLED;
    else if(o.st==ORD_ACCEPTED||o.st==ORD_SUBMITTED||o.st==ORD_INIT) o.st = ORD_PART; // 乱序：trade 先到也推进
}

static int g_fail=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++g_fail; } }while(0)

int main(){
    // 正常：接受→部成→全成
    { Order o{}; o.volTotal=10;
      on_event(o,EV_ACCEPTED,"S1"); CHECK(o.st==ORD_ACCEPTED && !strcmp(o.sysId,"S1"));
      on_trade(o,4); CHECK(o.st==ORD_PART && o.volTraded==4);
      on_trade(o,6); CHECK(o.st==ORD_FILLED && o.volTraded==10); }

    // 撤单
    { Order o{}; o.volTotal=10; on_event(o,EV_ACCEPTED,"S2"); on_event(o,EV_CANCELED,"S2"); CHECK(o.st==ORD_CANCELED); }

    // 废单
    { Order o{}; o.volTotal=10; on_event(o,EV_REJECTED,"S3"); CHECK(o.st==ORD_REJECTED); }

    // 幂等：终态后重复成交/事件不再改
    { Order o{}; o.volTotal=10; on_trade(o,10); CHECK(o.st==ORD_FILLED);
      on_trade(o,10); CHECK(o.volTraded==10);              // 不再累计
      on_event(o,EV_ACCEPTED,"S4"); CHECK(o.st==ORD_FILLED); } // 终态不降

    // 乱序：成交先于 Accepted 到
    { Order o{}; o.volTotal=10; on_trade(o,3); CHECK(o.st==ORD_PART);
      on_event(o,EV_ACCEPTED,"S5"); CHECK(o.st==ORD_PART && !strcmp(o.sysId,"S5")); } // 不降级、但补 sysId

    // 超额成交保护（脏回报）
    { Order o{}; o.volTotal=10; on_trade(o,12); CHECK(o.st==ORD_FILLED); }

    printf(g_fail?"FAILED:%d\n":"SM ALL PASS\n", g_fail);
    return g_fail?1:0;
}
