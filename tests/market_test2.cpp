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

int main() {
    MarketStore store; MarketVisitor v(store);

    uint8_t b1[112] = {0};
    int32_t mult=5;        std::memcpy(b1+72,  &mult,  4);
    double  tick=5.0;      std::memcpy(b1+92,  &tick,  8);
    double  codec=18000.0; std::memcpy(b1+100, &codec, 8);
    int32_t ino=20;        std::memcpy(b1+108, &ino,   4);
    v.OnDomain(DomainRef{0x0101,112,b1});

    uint8_t lv[17]={0};
    std::memcpy(lv,&ino,4); lv[4]='0';
    double p=18000.0; std::memcpy(lv+5,&p,8);
    int32_t vol=1;     std::memcpy(lv+13,&vol,4);
    v.OnDomain(DomainRef{0x0103,17,lv});

    std::vector<uint8_t> h; put_zz(h,20); put_zz(h,3);
    v.OnDomain(DomainRef{0x0003,(int16_t)h.size(),h.data()});

    std::vector<uint8_t> e; e.push_back('2'); e.push_back('0');
    put_zz(e,1); put_zz(e,0); put_zz(e,5);
    v.OnDomain(DomainRef{0x1001,(int16_t)e.size(),e.data()});

    std::vector<uint8_t> s; put_zz(s,1); put_zz(s,10); put_zz(s,0); put_zz(s,0);
    v.OnDomain(DomainRef{0x1002,(int16_t)s.size(),s.data()});

    auto& st = store.Get(20);
    CHECK(st.book.Bid[0].Volume == 5);
    CHECK(st.trade.LastPrice == 18005.0);
    CHECK(st.trade.Volume == 10);
    CHECK(st.trade.Turnover == 900000.0);

    std::vector<uint8_t> h4; put_zz(h4,20); put_zz(h4,4);
    v.OnDomain(DomainRef{0x0003,(int16_t)h4.size(),h4.data()});
    CHECK(st.gaps == 0);

    std::vector<uint8_t> h7; put_zz(h7,20); put_zz(h7,7);
    v.OnDomain(DomainRef{0x0003,(int16_t)h7.size(),h7.data()});
    CHECK(st.gaps == 1);

    printf("last=%.0f vol=%d turnover=%.0f bid1=%.0f/%lld gaps=%u\n",
           st.trade.LastPrice, st.trade.Volume, st.trade.Turnover,
           st.book.Bid[0].Price, (long long)st.book.Bid[0].Volume, st.gaps);
    printf(g_fail?"FAILED: %d\n":"ALL PASS\n", g_fail);
    return g_fail?1:0;
}

