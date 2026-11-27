#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <algorithm>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <sched.h>
#include <pthread.h>
#include <x86intrin.h>

template<class T,size_t N> struct SPSC {
    static_assert((N&(N-1))==0,"");
    alignas(64) std::atomic<size_t> head{0}, tail{0};
    alignas(64) T buf[N];
    bool push(const T& v){ size_t h=head.load(std::memory_order_relaxed),t=tail.load(std::memory_order_acquire);
        if(((h-t)&(N-1))==N-1) return false; buf[h]=v; head.store((h+1)&(N-1),std::memory_order_release); return true; }
    bool pop(T& v){ size_t t=tail.load(std::memory_order_relaxed);
        if(t==head.load(std::memory_order_acquire)) return false; v=buf[t]; tail.store((t+1)&(N-1),std::memory_order_release); return true; }
};
static void pin(int c){ cpu_set_t s; CPU_ZERO(&s); CPU_SET(c,&s); pthread_setaffinity_np(pthread_self(),sizeof s,&s); }

struct Position{ int64_t qty=0; double avg=0,realized=0; };
static void update_pos(Position&p,bool buy,double px,int64_t q){
    if(buy){ if(p.qty>=0){p.avg=(p.avg*p.qty+px*q)/(p.qty+q);p.qty+=q;}
             else{int64_t c=std::min<int64_t>(q,-p.qty);p.realized+=(p.avg-px)*c;p.qty+=c;int64_t r=q-c;if(r>0){p.avg=px;p.qty+=r;}if(p.qty==0)p.avg=0;}}
    else{ if(p.qty<=0){int64_t aq=-p.qty;p.avg=(p.avg*aq+px*q)/(aq+q);p.qty-=q;}
          else{int64_t c=std::min<int64_t>(q,p.qty);p.realized+=(px-p.avg)*c;p.qty-=c;int64_t r=q-c;if(r>0){p.avg=px;p.qty-=r;}if(p.qty==0)p.avg=0;}}
}
struct Tick{ double bid,ask; uint64_t t_rx; };

int main(){
    const int N=4000000;
    SPSC<Tick,1<<20> q;                          // 大 ring，减少积压
    std::vector<uint64_t> lat; lat.reserve(N);
    std::atomic<bool> prod_done{false};

    std::thread prod([&]{ pin(2); double mid=778.0;
        for(int i=0;i<N;i++){ mid += ((i%7)-3)*0.02;
            Tick t{ mid-0.10, mid+0.10, __rdtsc() };
            int spin=0; while(!q.push(t)){ if(++spin>100000000) break; }
        } prod_done.store(true,std::memory_order_release);
    });
    std::thread cons([&]{ pin(3); Position pos; double prev=0; Tick t;
        while(!(prod_done.load(std::memory_order_acquire)&&lat.size()>=(size_t)N)){
            if(!q.pop(t)){ _mm_pause(); continue; }
            double m=(t.bid+t.ask)/2; int sig=(prev==0.0)?0:(m>prev?1:(m<prev?-1:0)); prev=m;
            if(sig){ bool buy=sig>0; double px=buy?t.ask:t.bid; update_pos(pos,buy,px,1); }
            lat.push_back(__rdtsc()-t.t_rx);
        }
        (void)pos;
    });
    prod.join(); cons.join();

    std::sort(lat.begin(),lat.end());
    auto pct=[&](double p){ return (double)lat[(size_t)(p*((double)lat.size()-1))]/3.0; };
    printf("跨核 tick->账本完: p50=%.1f p99=%.1f p999=%.1f ns (n=%zu)\n",pct(0.5),pct(0.99),pct(0.999),lat.size());
    return 0;
}
