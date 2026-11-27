// demo_trade4.cpp —— demo_trade3 + 限速回放(pace) + SCHED_FIFO，测真实 p99
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>
#include <rte_cycles.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <atomic>
#include <thread>
#include <vector>
#include <map>
#include <deque>
#include <functional>
#include <algorithm>
#include <chrono>
#include <sched.h>
#include <pthread.h>
#include <x86intrin.h>
#include "mirp.h"
#include "decode.h"
#include "delta.h"
#include "wire_frame.h"

struct Collected{ smdp::ContractDeltaHead H{}; smdp::PriceLevelDelta PL{}; smdp::TradeSummary TS{}; int n=0;
    void OnDomain(const smdp::DomainRef& d){ switch(d.FieldID){
        case 0x0003: smdp::DecodeContractHead(d,H); break;
        case 0x1001: smdp::DecodePriceLevelDelta(d,PL); break;
        case 0x1002: smdp::DecodeTradeSummary(d,TS); break; } ++n; } };
template<class T,size_t N> struct Spsc{
    alignas(64) std::atomic<size_t> head{0},tail{0}; alignas(64) T buf[N];
    bool push(const T& v){ size_t t=tail.load(std::memory_order_relaxed),n=(t+1)&(N-1);
        if(n==head.load(std::memory_order_acquire))return false; buf[t]=v; tail.store(n,std::memory_order_release); return true; }
    bool pop(T& v){ size_t h=head.load(std::memory_order_relaxed);
        if(h==tail.load(std::memory_order_acquire))return false; v=buf[h]; head.store((h+1)&(N-1),std::memory_order_release); return true; } };
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

static void set_rt(){ struct sched_param sp; sp.sched_priority=80;
    if(sched_setscheduler(0,SCHED_FIFO,&sp)!=0) std::perror("SCHED_FIFO(fail, 需root)"); }

struct TickMsg{ uint32_t instr; uint64_t t_rx; };
static Spsc<TickMsg,1024> tick_ring;
static std::atomic<bool> g_done{false};
static std::atomic<long> g_decoded{0};
static double g_interval_cyc=0;   // 每 tick 间隔(cycles)，由 main 设

int main(int argc,char**argv){
    double rate = 5000.0;          // tick/s，可改
    if(const char* e=getenv("RATE")) rate=atof(e);
    if(rte_eal_init(argc,argv)<0) return 1;
    if(rte_eth_dev_count_avail()<1){ printf("no port\n"); return 1; }
    struct rte_eth_conf conf; __builtin_memset(&conf,0,sizeof conf);
    if(rte_eth_dev_configure(0,1,0,&conf)<0){printf("cfg fail\n");return 1;}
    struct rte_mempool* mp=rte_pktmbuf_pool_create("mp",16384,0,0,2048,rte_socket_id());
    struct rte_eth_rxconf rxq; __builtin_memset(&rxq,0,sizeof rxq);
    if(rte_eth_rx_queue_setup(0,0,1024,rte_socket_id(),&rxq,mp)<0){printf("rxq fail\n");return 1;}
    if(rte_eth_dev_start(0)<0){printf("start fail\n");return 1;}
    rte_eth_promiscuous_enable(0);
    uint64_t c0=__rdtsc(); std::this_thread::sleep_for(std::chrono::milliseconds(200)); uint64_t c1=__rdtsc();
    double tsc_ghz=(double)(c1-c0)/0.2e9;
    g_interval_cyc = tsc_ghz*1e9/rate;
    printf("tsc=%.2fGHz pace=%.0f ticks/s (interval %.0f ns)\n", tsc_ghz, rate, 1e9/rate);

    std::thread rx([]{ set_rt(); cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(2,&cs); pthread_setaffinity_np(pthread_self(),sizeof cs,&cs);
        struct rte_mbuf* m[64]; long idle=0; uint64_t next=__rdtsc();
        for(long it=0;it<200000000;++it){ uint16_t n=rte_eth_rx_burst(0,0,m,64);
            if(!n){ if(++idle>5000000) break; continue; } idle=0;
            for(uint16_t j=0;j<n;++j){ wire::View v{};
                if(wire::ParseUdpFrame(rte_pktmbuf_mtod(m[j],const uint8_t*),rte_pktmbuf_pkt_len(m[j]),v)){
                    Collected c; if(smdp::IterateDomainsT(v.payload,v.len,c)>=0 && c.H.InstrumentNo>=1 && c.H.InstrumentNo<=4){
                        while(__rdtsc()<next) _mm_pause();           // 限速: 到点才放行
                        TickMsg t{(uint32_t)c.H.InstrumentNo,__rdtsc()}; tick_ring.push(t);
                        next += (uint64_t)g_interval_cyc; ++g_decoded; } }
                rte_pktmbuf_free(m[j]); } }
        g_done.store(true); });

    long orders=0,fills=0; long icnt[5]={0,0,0,0,0}; Pos pos[5]; std::vector<uint32_t> lat;
    std::thread tr([&]{ set_rt(); cpu_set_t cs; CPU_ZERO(&cs); CPU_SET(3,&cs); pthread_setaffinity_np(pthread_self(),sizeof cs,&cs);
        Book bk[5]; seed(bk); std::vector<std::pair<int64_t,int32_t>> fl;
        for(;;){ TickMsg t;
            if(tick_ring.pop(t)){ uint32_t ins=t.instr; bool buy=((icnt[ins]&1)==0); ++icnt[ins]; ++orders;
                fl.clear(); int32_t got=match_mkt(bk[ins],buy,1,fl);
                if(got){ for(auto&x:fl) on_fill(pos[ins],buy,x.first,x.second); fills+=fl.size();
                    lat.push_back((uint32_t)(__rdtsc()-t.t_rx)); } }
            else if(g_done.load()) break; } });

    rx.join(); tr.join();
    std::sort(lat.begin(),lat.end());
    auto pct=[&](double p){ return lat.empty()?0.0:lat[(size_t)(p*(lat.size()-1))]/tsc_ghz; };
    double ureal=0,rlz=0; for(int k=1;k<=4;++k){ ureal+=(pos[k].mark-pos[k].avg)*pos[k].qty; rlz+=pos[k].realized; }
    printf("decoded=%ld orders=%ld fills=%ld\n",g_decoded.load(),orders,fills);
    printf("合计 realized=%.1f unrealized=%.1f\n",rlz,ureal);
    if(!lat.empty()) printf("tick->成交(ns): min=%.0f p50=%.0f p90=%.0f p99=%.0f max=%.0f (n=%zu)\n",
        lat.front()/tsc_ghz,pct(0.5),pct(0.9),pct(0.99),lat.back()/tsc_ghz,lat.size());
    return 0;
}
