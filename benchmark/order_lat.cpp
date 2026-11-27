#include "ThostFtdcUserApiStruct.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <chrono>
#include <thread>
#include <x86intrin.h>

struct Tick { char inst[16]; double bid, ask; };

static char DIG2[200];
struct D2init { D2init(){ for(int i=0;i<100;i++){ DIG2[i*2]='0'+(i/10); DIG2[i*2+1]='0'+(i%10);} } } _d2i;
static inline int u64toa(char* out, uint64_t v){
    char t[24]; int n=0;
    while(v>=100){ uint64_t r=v%100; v/=100; t[n++]=DIG2[r*2+1]; t[n++]=DIG2[r*2]; }
    if(v>=10){ t[n++]=DIG2[v*2+1]; t[n++]=DIG2[v*2]; } else t[n++]='0'+char(v);
    int i=0; while(n) out[i++]=t[--n]; out[i]=0; return i;
}
static inline void inc_ascii(char* s, int n){          // 连续订单号 +1（无除法）
    for(int i=n-1;i>=0;--i){ if(s[i]<'9'){ ++s[i]; return; } s[i]='0'; }
}
static double tsc_ghz(){
    auto a=std::chrono::steady_clock::now(); uint64_t c0=__rdtsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    uint64_t c1=__rdtsc(); auto b=std::chrono::steady_clock::now();
    return (double)(c1-c0)/std::chrono::duration<double>(b-a).count()/1e9;
}
static void mk_base(CThostFtdcInputOrderField& o, const Tick& t, uint64_t seq, char dir){
    std::memset(&o, 0, sizeof o);
    std::memcpy(o.InstrumentID, t.inst, sizeof t.inst);
    u64toa(o.OrderRef, seq);
    o.Direction = dir; o.CombOffsetFlag[0]='0';
    o.LimitPrice = (dir=='0') ? t.ask : t.bid;
    o.VolumeTotalOriginal = 1;
    o.OrderPriceType='2'; o.TimeCondition='3'; o.VolumeCondition='1'; o.MinVolume=1;
    o.ContingentCondition='1'; o.ForceCloseReason='0';
}
int main(){
    double ghz = tsc_ghz();
    Tick t{}; std::strcpy(t.inst,"au2612"); t.bid=778.7; t.ask=778.8;
    CThostFtdcInputOrderField base{}; mk_base(base,t,1,'0');
    std::strcpy(base.OrderRef, "000000000001");        // 固定 12 位连续号
    const uint64_t N=2000000; volatile uint64_t sink=0;

    { CThostFtdcInputOrderField o;
      uint64_t a=__rdtsc();
      for(uint64_t i=0;i<N;i++){ mk_base(o,t,i,(i&1)?'0':'1'); sink+=(uint64_t)o.LimitPrice; }
      uint64_t b=__rdtsc();
      printf("baseline memset+fill+divitoa : %.2f ns/op\n", (double)(b-a)/N/ghz); }

    { CThostFtdcInputOrderField o = base;
      uint64_t a=__rdtsc();
      for(uint64_t i=0;i<N;i++){ char d=(i&1)?'0':'1'; o.Direction=d; o.LimitPrice=(d=='0')?t.ask:t.bid; inc_ascii(o.OrderRef,12); sink+=(uint64_t)o.LimitPrice; }
      uint64_t b=__rdtsc();
      printf("template-reuse inc-orderno  : %.2f ns/op\n", (double)(b-a)/N/ghz); }

    printf("sizeof=%zu tsc=%.3fGHz sink=%llu\n", sizeof(CThostFtdcInputOrderField), ghz, (unsigned long long)sink);
    return 0;
}
