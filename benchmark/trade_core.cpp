// trade_core.cpp —— 交易引擎核心(无行情/无DPDK): 报单帧+风控+撮合+持仓PNL
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <map>
#include <deque>
#include <functional>
#include <algorithm>
#include <chrono>

static constexpr int FRAME=32;
enum { T_NEW=1 }; enum { BUY=0, SELL=1 };
struct Frame{ uint8_t b[FRAME]; };
static inline void pu32(uint8_t*p,uint32_t v){std::memcpy(p,&v,4);}
static inline void pi32(uint8_t*p,int32_t v){std::memcpy(p,&v,4);}
static inline void pi64(uint8_t*p,int64_t v){std::memcpy(p,&v,8);}
static inline uint32_t gu32(const uint8_t*p){uint32_t v;std::memcpy(&v,p,4);return v;}
static inline int32_t  gi32(const uint8_t*p){int32_t v;std::memcpy(&v,p,4);return v;}
static inline int64_t  gi64(const uint8_t*p){int64_t v;std::memcpy(&v,p,8);return v;}
static Frame enc_new(uint32_t ref,uint32_t ins,uint8_t side,int64_t price,int32_t qty){
    Frame f; std::memset(f.b,0,FRAME); f.b[0]=T_NEW; f.b[1]=side;
    pu32(f.b+4,ref); pu32(f.b+8,ins); pi32(f.b+12,qty); pi64(f.b+16,price); return f;
}
struct Resting{ uint32_t ref; int32_t qty; };
struct Book{ std::map<int64_t,std::deque<Resting>,std::greater<int64_t>> bids;
             std::map<int64_t,std::deque<Resting>> asks; };

static void seed(Book bk[5]){                       // 少档位、巨额、正价格
    for(int ins=1;ins<=4;++ins){ int64_t b=ins*1000;
        for(int k=0;k<5;++k)  bk[ins].asks[b+k*10].push_back({0,100000000});
        for(int k=1;k<=5;++k) bk[ins].bids[b-k*10].push_back({0,100000000}); }
}
static int32_t match(Book& bk,bool buy,int64_t limit,int32_t qty,std::vector<std::pair<int64_t,int32_t>>& fl){
    int32_t left=qty;
    if(buy){ while(left>0 && !bk.asks.empty()){ auto it=bk.asks.begin();
        if(limit>0 && it->first>limit) break; auto& dq=it->second;
        while(left>0&&!dq.empty()){ auto&r=dq.front(); int32_t t=std::min(left,r.qty); fl.push_back({it->first,t});
            left-=t; r.qty-=t; if(r.qty==0)dq.pop_front(); else break; } if(dq.empty())bk.asks.erase(it);} }
    else { while(left>0 && !bk.bids.empty()){ auto it=bk.bids.begin();
        if(limit>0 && it->first<limit) break; auto& dq=it->second;
        while(left>0&&!dq.empty()){ auto&r=dq.front(); int32_t t=std::min(left,r.qty); fl.push_back({it->first,t});
            left-=t; r.qty-=t; if(r.qty==0)dq.pop_front(); else break; } if(dq.empty())bk.bids.erase(it);} }
    return qty-left;
}
struct Pos{ int64_t qty=0; double avg=0, realized=0, mark=0; };
static void on_fill(Pos& p,bool buy,int64_t price,int32_t q){
    if(buy){
        if(p.qty>=0){ p.avg=(p.avg*p.qty+(double)price*q)/(p.qty+q); p.qty+=q; }
        else{ int64_t c=std::min<int64_t>(q,-p.qty); p.realized+=(p.avg-(double)price)*c; p.qty+=c;
              int64_t rem=q-c; if(rem>0){p.avg=(double)price;p.qty+=rem;} if(p.qty==0)p.avg=0; }
    }else{
        if(p.qty<=0){ int64_t aq=-p.qty; p.avg=(p.avg*aq+(double)price*q)/(aq+q); p.qty-=q; }
        else{ int64_t c=std::min<int64_t>(q,p.qty); p.realized+=((double)price-p.avg)*c; p.qty-=c;
              int64_t rem=q-c; if(rem>0){p.avg=(double)price;p.qty-=rem;} if(p.qty==0)p.avg=0; }
    }
    p.mark=(double)price;
}
static constexpr int MAX_QTY=1000;
int main(int argc,char**argv){
    long N = argc>1?atol(argv[1]):200000;
    Book bk[5]; seed(bk);
    Pos pos[5]; long orders=0,fills=0,rejects=0; long icnt[5]={0,0,0,0,0};
    std::vector<std::pair<int64_t,int32_t>> fl;
    auto t0=std::chrono::steady_clock::now();
    for(long i=0;i<N;++i){
        uint32_t ref=(uint32_t)i+1, ins=(uint32_t)(i%4)+1;
        bool buy=((icnt[ins]&1)==0);                    // 每合约交替买/卖
        int32_t qty=((i%1000)==999)?5000:1;
        Frame f=enc_new(ref,ins,buy?BUY:SELL,0,qty);
        uint32_t ii=gu32(f.b+8); uint8_t sd=f.b[1]; int32_t q=gi32(f.b+12); int64_t px=gi64(f.b+16);
        if(q>MAX_QTY){ ++rejects; continue; }           // 风控
        ++icnt[ins]; ++orders;
        fl.clear(); int32_t got=match(bk[ii], sd==BUY, px, q, fl);
        if(got){ for(auto&x:fl) on_fill(pos[ii], sd==BUY, x.first, x.second); fills+=fl.size(); }
    }
    auto t1=std::chrono::steady_clock::now();
    double sec=std::chrono::duration<double>(t1-t0).count();
    double ureal=0, rlz=0; for(int k=1;k<=4;++k){ ureal+=(pos[k].mark-pos[k].avg)*pos[k].qty; rlz+=pos[k].realized; }
    printf("N=%ld orders=%ld fills=%ld rejects=%ld  %.1f Morders/s\n",N,orders,fills,rejects,orders/sec/1e6);
    for(int k=1;k<=4;++k) printf("  [%d] qty=%ld avg=%.2f rlz=%.1f mark=%.1f\n",k,pos[k].qty,pos[k].avg,pos[k].realized,pos[k].mark);
    printf("合计 realized=%.1f unrealized=%.1f\n", rlz, ureal);
    return 0;
}
