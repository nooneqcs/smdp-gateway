#include "market2.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
using namespace smdp;

static int g_fail=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++g_fail;} }while(0)

static void put_zz(std::vector<uint8_t>& b, int64_t v){
    uint64_t zz=(static_cast<uint64_t>(v)<<1)^static_cast<uint64_t>(v>>63);
    while(zz>=0x80){ b.push_back((uint8_t)((zz&0x7F)|0x80)); zz>>=7; }
    b.push_back((uint8_t)zz);
}

int main(){
    MarketStore store; MarketVisitor v(store);
    for(int k=0;k<2;k++){ auto& s=store.Get(k); s.info.CodecPrice=18000; s.info.PriceTick=5; }

    // “一帧”里两个合约：ino0 的 0x0003+0x1001
    std::vector<uint8_t> h0; put_zz(h0,0); put_zz(h0,1);
    v.OnDomain(DomainRef{0x0003,(int16_t)h0.size(),h0.data()});
    std::vector<uint8_t> e0; e0.push_back('1'); e0.push_back('0'); put_zz(e0,1); put_zz(e0,0); put_zz(e0,3);
    v.OnDomain(DomainRef{0x1001,(int16_t)e0.size(),e0.data()});

    // 紧接着 ino1 的 0x0003+0x1001
    std::vector<uint8_t> h1; put_zz(h1,1); put_zz(h1,1);
    v.OnDomain(DomainRef{0x0003,(int16_t)h1.size(),h1.data()});
    std::vector<uint8_t> e1; e1.push_back('1'); e1.push_back('0'); put_zz(e1,1); put_zz(e1,0); put_zz(e1,7);
    v.OnDomain(DomainRef{0x1001,(int16_t)e1.size(),e1.data()});

    CHECK(v.Touched().size()==2);                         // 一帧两合约，都记到
    CHECK(store.Get(0).book.Bid[0].Volume==3);
    CHECK(store.Get(1).book.Bid[0].Volume==7);

    printf("touched=%zu  ino0.bid0=%lld  ino1.bid0=%lld\n",
        v.Touched().size(),(long long)store.Get(0).book.Bid[0].Volume,
        (long long)store.Get(1).book.Bid[0].Volume);
    for(auto ino : v.Touched()) printf("  touched ino=%lld\n",(long long)ino);
    printf(g_fail?"FAILED: %d\n":"ALL PASS\n", g_fail);
    return g_fail?1:0;
}

