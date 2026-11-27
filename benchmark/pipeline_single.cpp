#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <chrono>
#include <thread>
#include <x86intrin.h>

// ① 状态机
enum OrdStatus{ORD_INIT,ORD_SUBMITTED,ORD_ACCEPTED,ORD_PART,ORD_FILLED,ORD_CANCELED,ORD_REJECTED};
struct Order{ int64_t volTotal=0,volTraded=0; OrdStatus st=ORD_INIT; };
static inline bool term(const Order&o){ return o.st==ORD_FILLED||o.st==ORD_CANCELED||o.st==ORD_REJECTED; }
static void on_trade(Order&o,int64_t v){ if(term(o)||v<=0)return; o.volTraded+=v; o.st=(o.volTraded>=o.volTotal)?ORD_FILLED:ORD_PART; }

// ② 账本
struct Position{ int64_t qty=0; double avg=0,realized=0; };
static void update_pos(Position&p,bool buy,double px,int64_t q){
    if(buy){ if(p.qty>=0){p.avg=(p.avg*p.qty+px*q)/(p.qty+q);p.qty+=q;}
             else{int64_t c=std::min<int64_t>(q,-p.qty);p.realized+=(p.avg-px)*c;p.qty+=c;int64_t r=q-c;if(r>0){p.avg=px;p.qty+=r;}if(p.qty==0)p.avg=0;}}
    else{ if(p.qty<=0){int64_t aq=-p.qty;p.avg=(p.avg*aq+px*q)/(aq+q);p.qty-=q;}
          else{int64_t c=std::min<int64_t>(q,p.qty);p.realized+=(px-p.avg)*c;p.qty-=c;int64_t r=q-c;if(r>0){p.avg=px;p.qty-=r;}if(p.qty==0)p.avg=0;}}
}
// ③ 风控
struct RiskCfg{ int64_t maxLots=100,maxPos=1000; double cage=0.10; };
struct RiskState{ int64_t pos=0; };
static bool risk_ok(const RiskCfg&c,RiskState&s,bool buy,double px,int64_t v,double last){
    if(v<=0||v>c.maxLots) return false;
    if(std::llabs(s.pos+(buy?v:-v))>c.maxPos) return false;
    if(last>0&&std::fabs(px-last)/last>c.cage) return false;
    return true;
}
struct Tick{ double bid,ask; uint64_t t_rx; };

static double tsc_ghz(){
    auto a=std::chrono::steady_clock::now(); uint64_t c0=__rdtsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    uint64_t c1=__rdtsc(); auto b=std::chrono::steady_clock::now();
    return (double)(c1-c0)/std::chrono::duration<double>(b-a).count()/1e9;
}

int main(){
    double ghz=tsc_ghz();
    Position pos; RiskCfg c; RiskState rs;
    double mid=778.0, prev=0;
    const int N=4000000;
    std::vector<uint64_t> lat; lat.reserve(N);
    long orders=0,fills=0,rej=0;

    for(int i=0;i<N;i++){
        mid += ((i%7)-3)*0.02;
        Tick t{ mid-0.10, mid+0.10, __rdtsc() };     // tick 到手（同核 t_rx）
        double m=(t.bid+t.ask)/2;
        int sig=(prev==0.0)?0:(m>prev?1:(m<prev?-1:0)); prev=m;
        if(sig){
            bool buy=sig>0; double px=buy?t.ask:t.bid; ++orders;
            if(!risk_ok(c,rs,buy,px,1,m)){ ++rej; }
            else { Order o; o.volTotal=1; on_trade(o,1);
                   if(o.st==ORD_FILLED){ update_pos(pos,buy,px,1); ++fills; }
                   rs.pos += buy?1:-1; }
        }
        lat.push_back(__rdtsc()-t.t_rx);             // 同核：tick到手 → 处理完（无跨核）
    }
    std::sort(lat.begin(),lat.end());
    auto pct=[&](double p){ return (double)lat[(size_t)(p*(lat.size()-1))]/ghz; };
    printf("orders=%ld fills=%ld rej=%ld qty=%lld realized=%.1f\n", orders,fills,rej,(long long)pos.qty,pos.realized);
    printf("同核 tick->账本完: p50=%.1f p99=%.1f p999=%.1f ns (n=%zu, tsc=%.3fGHz)\n",
           pct(0.5),pct(0.99),pct(0.999),lat.size(),ghz);
    return 0;
}
