#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <algorithm>

// ① 状态机
enum OrdStatus { ORD_INIT,ORD_SUBMITTED,ORD_ACCEPTED,ORD_PART,ORD_FILLED,ORD_CANCELED,ORD_REJECTED };
enum OrdEvent  { EV_ACCEPTED,EV_PARTIAL,EV_FILLED,EV_CANCELED,EV_REJECTED };
struct Order { uint64_t ref=0; char sysId[32]{}; OrdStatus st=ORD_INIT; int64_t volTotal=0,volTraded=0; double price=0; };
static inline bool term(const Order&o){ return o.st==ORD_FILLED||o.st==ORD_CANCELED||o.st==ORD_REJECTED; }
static void on_event(Order&o,OrdEvent ev,const char*sid){ if(sid&&*sid&&!o.sysId[0])std::strncpy(o.sysId,sid,31); if(term(o))return;
    switch(ev){case EV_ACCEPTED:if(o.st==ORD_INIT||o.st==ORD_SUBMITTED)o.st=ORD_ACCEPTED;break;
               case EV_PARTIAL:o.st=ORD_PART;break; case EV_FILLED:o.st=ORD_FILLED;break;
               case EV_CANCELED:o.st=ORD_CANCELED;break; case EV_REJECTED:o.st=ORD_REJECTED;break;} }
static void on_trade(Order&o,int64_t v){ if(term(o)||v<=0)return; o.volTraded+=v;
    if(o.volTraded>=o.volTotal)o.st=ORD_FILLED; else if(o.st==ORD_ACCEPTED||o.st==ORD_SUBMITTED||o.st==ORD_INIT)o.st=ORD_PART; }

// ② 账本
struct Position { int64_t qty=0; double avg=0,realized=0; };
struct Account  { double init=0,commission=0,realized=0,unrealized=0; double equity()const{return init+realized+unrealized-commission;} };
static void update_pos(Position&p,bool buy,double px,int64_t q){
    if(buy){ if(p.qty>=0){p.avg=(p.avg*p.qty+px*q)/(p.qty+q);p.qty+=q;}
             else{int64_t c=std::min<int64_t>(q,-p.qty);p.realized+=(p.avg-px)*c;p.qty+=c;int64_t r=q-c;if(r>0){p.avg=px;p.qty+=r;}if(p.qty==0)p.avg=0;}}
    else   { if(p.qty<=0){int64_t aq=-p.qty;p.avg=(p.avg*aq+px*q)/(aq+q);p.qty-=q;}
             else{int64_t c=std::min<int64_t>(q,p.qty);p.realized+=(px-p.avg)*c;p.qty-=c;int64_t r=q-c;if(r>0){p.avg=px;p.qty-=r;}if(p.qty==0)p.avg=0;}}
}
static void on_fill(Position&p,Account&a,bool buy,double px,int64_t q,double fee){ update_pos(p,buy,px,q); a.realized=p.realized; a.commission+=fee*q; }

// ③ 风控
enum RR{OK,REJ_LOTS,REJ_POS,REJ_CAGE,REJ_SELF,REJ_RATE};
struct RiskCfg{ int64_t maxLotsPerOrder=100,maxPosition=1000; double cagePct=0.10; int maxPerSec=1000000; };
struct RiskState{ int64_t pos=0; uint64_t winStart=0; int winCount=0; };
static RR risk(const RiskCfg&c,RiskState&s,bool buy,double px,int64_t v,double last,uint64_t now){
    if(v<=0||v>c.maxLotsPerOrder)return REJ_LOTS;
    if(std::llabs(s.pos+(buy?v:-v))>c.maxPosition)return REJ_POS;
    if(last>0&&std::fabs(px-last)/last>c.cagePct)return REJ_CAGE;
    if(buy&&last>0&&px>=last+c.cagePct*0){} // 占位（自成交需本方挂单，这里无挂单不触发）
    if(now-s.winStart>=1000000000ULL){s.winStart=now;s.winCount=0;}
    if(s.winCount>=c.maxPerSec)return REJ_RATE;
    return OK;
}

int main(){
    Position pos; Account acct; acct.init=100000;
    RiskCfg cfg; RiskState rs;
    double prev=778.00, last=prev;
    long orders=0, fills=0, rej=0; int64_t net=0;

    for(int i=0;i<2000;i++){
        last = prev + ((i%7)-3)*0.02;              // 合成 tick 小步走
        double bid=last-0.10, ask=last+0.10;
        int sig = (last>prev)?1:(last<prev?-1:0);
        prev = last;
        if(!sig) continue;
        bool buy = (sig>0); int64_t vol=1; ++orders;
        double px = buy?ask:bid;
        if(risk(cfg,rs,buy,px,vol,last,0)!=OK){ ++rej; continue; }

        Order o; o.ref=i; o.volTotal=vol; o.price=px;
        on_event(o,EV_ACCEPTED,"SIM");             // mock 柜台受理
        on_trade(o,vol);                           // mock 立即全成
        if(o.st==ORD_FILLED){ on_fill(pos,acct,buy,px,vol,0.5); ++fills; net += buy?vol:-vol; }

        rs.pos += buy?vol:-vol;
    }
    acct.unrealized = (last - pos.avg)*pos.qty; acct.realized = pos.realized;
    printf("orders=%ld fills=%ld rej=%ld pos.qty=%lld(net=%lld) avg=%.2f realized=%.1f unrealized=%.1f commission=%.1f equity=%.1f last=%.2f\n",
        orders,fills,rej,(long long)pos.qty,(long long)net,pos.avg,acct.realized,acct.unrealized,acct.commission,acct.equity(),last);
    bool selfconsistent = (pos.qty==net);
    printf("self-consistent(qty==net): %s\n", selfconsistent?"YES":"NO");
    return selfconsistent?0:1;
}
