// hot_bench.cpp —— 40B MIRP 报单热缓存微基准（三层拆解）
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <chrono>
#include "mirp.h"
#include "market2.h"

using namespace smdp;

static inline bool ValidMirp(const uint8_t* m, uint32_t mlen, MirpHeader& h){
    if(mlen < 24+4) return false;
    if(ParseMirpHeader(m, mlen, h) != 0) return false;
    if(h.TypeID != 0x01 || (h.Flag & 0x0F) != 1) return false;
    if(h.TopicID < 1000 || h.TopicID > 9999) return false;
    if((uint32_t)h.Length + 24 != mlen) return false;
    return true;
}

int main(){
    static uint8_t buf[64];
    const char* hex="0101100002000000E903000002000000778B0D4FB42D000003000200000401100600313002000200";
    for(int i=0;i<40;i++){ unsigned v; sscanf(hex+2*i,"%2x",&v); buf[i]=(uint8_t)v; }

    MarketStore store; MarketVisitor mv(store);
    for(int i=0;i<1000;i++){auto&s=store.Get(i); s.info.CodecPrice=18000.0; s.info.PriceTick=5.0;}

    const long N=10000000;
    MirpHeader h;
    volatile long long sink=0;

    // 预热
    for(int i=0;i<100000;i++){ if(ValidMirp(buf,40,h)) IterateDomainsT(buf+24,h.Length,mv); }

    // 1) 只做 ValidMirp
    auto t0=std::chrono::steady_clock::now();
    long okc=0; for(long i=0;i<N;i++){ if(ValidMirp(buf,40,h)) okc++; }
    auto t1=std::chrono::steady_clock::now();
    sink+=okc;
    double ns1=std::chrono::duration<double,std::nano>(t1-t0).count()/N;

    // 2) 只做域解码+合并
    t0=std::chrono::steady_clock::now();
    for(long i=0;i<N;i++){ IterateDomainsT(buf+24,h.Length,mv); }
    t1=std::chrono::steady_clock::now();
    sink+=store.Get(0).book.Bid[0].Volume;
    double ns2=std::chrono::duration<double,std::nano>(t1-t0).count()/N;

    // 3) 完整单条（校验+解码+合并）
    t0=std::chrono::steady_clock::now();
    long full=0; for(long i=0;i<N;i++){ if(ValidMirp(buf,40,h)){ IterateDomainsT(buf+24,h.Length,mv); full++; } }
    t1=std::chrono::steady_clock::now();
    sink+=full;
    double ns3=std::chrono::duration<double,std::nano>(t1-t0).count()/N;

    printf("N=%ld 次, sink=%lld (防优化)\n", N, (long long)sink);
    printf("1) ValidMirp 头校验            : %.2f ns/条\n", ns1);
    printf("2) 域解码 + book 合并          : %.2f ns/条\n", ns2);
    printf("3) 完整单条（1+2）             : %.2f ns/条\n", ns3);
    printf("   首个合约 Bid1=%.4f vol=%lld\n",
           store.Get(0).book.Bid[0].Price,(long long)store.Get(0).book.Bid[0].Volume);
    return 0;
}
