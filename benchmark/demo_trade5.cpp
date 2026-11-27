#include "quote_snapshot.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstddef>
#include <cmath>
#include <vector>
#include <map>
#include <deque>
#include <functional>
#include <algorithm>
#include <thread>
#include <atomic>
#include <chrono>
#include <sched.h>
#include <pthread.h>
#include <x86intrin.h>

#pragma pack(push,1)
struct RecTickWire {
    char instrument[16]; int64_t update_time;
    double last_price, bid_price, ask_price, ref_price;
    int64_t bid_volume, ask_volume, volume, turnover; double open_interest;
};
#pragma pack(pop)
static_assert(sizeof(RecTickWire) == 96, "");

struct alignas(64) RecTick64 {
    int64_t update_time, turnover; char instrument[8];
    int32_t last_price, bid_price, ask_price, ref_price, open_interest;
    int32_t bid_volume, ask_volume, volume, trading_day;
    int16_t update_millisec; uint8_t exchange_id, flags;
};
static_assert(sizeof(RecTick64) == 64, "");

static inline int32_t to_fixed(double p, double inv){ return (int32_t)std::llround(p*inv); }
static RecTick64 compress(const RecTickWire& s, double inv){
    RecTick64 d{};
    std::memcpy(d.instrument, s.instrument, 8); d.instrument[7]=0;
    d.update_time=s.update_time; d.turnover=s.turnover;
    d.last_price=to_fixed(s.last_price,inv); d.bid_price=to_fixed(s.bid_price,inv);
    d.ask_price=to_fixed(s.ask_price,inv);   d.ref_price=to_fixed(s.ref_price,inv);
    d.open_interest=(int32_t)std::llround(s.open_interest);
    d.bid_volume=(int32_t)s.bid_volume; d.ask_volume=(int32_t)s.ask_volume;
    d.volume=(int32_t)s.volume; d.trading_day=20231114;
    d.update_millisec=0; d.exchange_id=0; d.flags=0;
    return d;
}

struct Resting{ uint32_t ref; int32_t qty; };
struct Book{ std::map<int64_t,std::deque<Resting>,std::greater<int64_t>> bids; std::map<int64_t,std::deque<Resting>> asks; };
static void seed(Book bk[5]){ for(int ins=1;ins<=4;++ins){ int64_t b=ins*1000;
    for(int k=0;k<5;++k) bk[ins].asks[b+k*10].push_back({0,100000000});
    for(int k=1;k<=5;++k) bk[ins].bids[b-k*10].push_back({0,100000000}); } }
static int32_t match_mkt(Book& bk,bool buy,int32_t qty,std::vector<std::pair<int64_t,int32_t>>& fl){
    int32_t left=qty;
    if(buy){ while(left>0&&!bk.asks.empty()){ auto it=bk.asks.begin(); auto&dq=it->second;
        while(left>0&&!dq.empty()){ auto&r=dq.front(); int32_t t=std::min(left,r.qty); fl.push_back({it->first,t}); left-=t; r.qty-=t; if(r.qty==0)dq.pop_front(); else break; } if(dq.empty())bk.asks.erase(it);} }
    else { while(left>0&&!bk.bids.empty()){ auto it=bk.bids.begin(); auto&dq=it->second;
        while(left>0&&!dq.empty()){ auto&r=dq.front(); int32_t t=std::min(left,r.qty); fl.push_back({it->first,t}); left-=t; r.qty-=t; if(r.qty==0)dq.pop_front(); else break; } if(dq.empty())bk.bids.erase(it);} }
    return qty-left; }
struct Pos{ int64_t qty=0; double avg=0,realized=0,mark=0; };
static void on_fill(Pos& p,bool buy,int64_t price,int32_t q){
    if(buy){ if(p.qty>=0){ p.avg=(p.avg*p.qty+(double)price*q)/(p.qty+q); p.qty+=q; }
        else{ int64_t c=std::min<int64_t>(q,-p.qty); p.realized+=(p.avg-(double)price)*c; p.qty+=c; int64_t r=q-c; if(r>0){p.avg=(double)price;p.qty+=r;} if(p.qty==0)p.avg=0; } }
    else{ if(p.qty<=0){ int64_t aq=-p.qty; p.avg=(p.avg*aq+(double)price*q)/(aq+q); p.qty-=q; }
        else{ int64_t c=std::min<int64_t>(q,p.qty); p.realized+=((double)price-p.avg)*c; p.qty-=c; int64_t r=q-c; if(r>0){p.avg=(double)price;p.qty-=r;} if(p.qty==0)p.avg=0; } }
    p.mark=(double)price; }

static int ins_of(const char* inst){
    if(!std::strncmp(inst,"au",2))return 1;
    if(!std::strncmp(inst,"ag",2))return 2;
    if(!std::strncmp(inst,"cu",2))return 3;
    if(!std::strncmp(inst,"rb",2))return 4;
    return 0;
}
static void pin(int cpu){
    cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(cpu,&cs);
    pthread_setaffinity_np(pthread_self(),sizeof cs,&cs);
}
static double tsc_ghz(){
    auto t0=std::chrono::steady_clock::now(); uint64_t c0=__rdtsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    uint64_t c1=__rdtsc(); auto t1=std::chrono::steady_clock::now();
    return (double)(c1-c0)/std::chrono::duration<double>(t1-t0).count()/1e9;
}

int main(int argc,char**argv){
    const char* path = argc>1?argv[1]:"ticks_simnow.bin";
    FILE* f=fopen(path,"rb"); if(!f){perror(path);return 1;}
    std::vector<RecTickWire> recs; RecTickWire r;
    while(fread(&r,sizeof r,1,f)==1) recs.push_back(r);
    fclose(f);
    const size_t n=recs.size(); if(!n)return 1;

    double rate=5000.0; if(const char* e=getenv("RATE")) rate=atof(e);
    double ghz=tsc_ghz();
    uint64_t interval=(uint64_t)(ghz*1e9/rate);
    const double tick=0.02, inv=1.0/tick;
    const size_t TOTAL=n*20;

    qgw::QuoteSnapshot<RecTick64> snap;
    std::atomic<bool> done{false};
    std::atomic<bool> ready{false};
    std::atomic<long> decoded{0};
    long orders=0,fills=0; Pos pos[5]; uint32_t icnt[5]={0};

    std::thread prod([&]{
        pin(2);
        while(!ready.load(std::memory_order_acquire)) _mm_pause();
        size_t i=0; uint64_t seq=0, next=__rdtsc();
        for(size_t k=0;k<TOTAL;++k){
            RecTick64 t=compress(recs[i],inv);
            t.update_time=(int64_t)seq++;
            snap.publish(t);
            if(++i==n)i=0;
            next+=interval; while(__rdtsc()<next)_mm_pause();
            decoded.fetch_add(1,std::memory_order_relaxed);
        }
        done.store(true,std::memory_order_release);
    });

    std::thread strat([&]{
        pin(3);
        Book bk[5]; seed(bk); std::vector<std::pair<int64_t,int32_t>> fl;
        ready.store(true,std::memory_order_release);
        RecTick64 t; uint64_t last=~0ull;
        for(;;){
            if(snap.latest(t)){
                uint64_t s=(uint64_t)t.update_time;
                if(s!=last){
                    last=s;
                    int ins=ins_of(t.instrument);
                    if(ins>=1&&ins<=4){
                        bool buy=((icnt[ins]&1)==0); ++icnt[ins]; ++orders;
                        fl.clear(); int32_t got=match_mkt(bk[ins],buy,1,fl);
                        if(got){ for(auto&x:fl) on_fill(pos[ins],buy,x.first,x.second); fills+=fl.size(); }
                    }
                }
            }
            if(done.load(std::memory_order_acquire) &&
               (uint64_t)decoded.load(std::memory_order_acquire) == last+1) break;
        }
    });

    prod.join(); strat.join();

    double ureal=0,rlz=0;
    for(int k=1;k<=4;++k){ ureal+=(pos[k].mark-pos[k].avg)*pos[k].qty; rlz+=pos[k].realized; }
    printf("decoded=%ld orders=%ld fills=%ld\n", decoded.load(), orders, fills);
    printf("合计 realized=%.1f unrealized=%.1f\n", rlz, ureal);
    printf("(RATE=%.0f tick/s, TOTAL=%zu)\n", rate, TOTAL);
    return 0;
}

