#include "market2.h"
#include "mirp.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <ctime>
#include <vector>
using namespace smdp;

static double now_ns(){ timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return ts.tv_sec*1e9+ts.tv_nsec; }

static std::vector<uint8_t> build_packet() {
    std::vector<uint8_t> p(24, 0);
    auto u16=[&](uint16_t v){ p.push_back(v&0xFF); p.push_back((v>>8)&0xFF); };
    p[0]=0x01; p[1]=0x01;
    u16(0x0003); u16(2); p.push_back(0x28); p.push_back(0x06);
    u16(0x1001); u16(5); p.push_back('2');p.push_back('0');p.push_back(0x02);p.push_back(0x00);p.push_back(0x0A);
    u16(0x1001); u16(5); p.push_back('2');p.push_back('0');p.push_back(0x04);p.push_back(0x00);p.push_back(0x14);
    u16(0x1002); u16(4); p.push_back(0x02);p.push_back(0x14);p.push_back(0x00);p.push_back(0x00);
    uint16_t body=(uint16_t)(p.size()-24);
    p[2]=body&0xFF; p[3]=(body>>8)&0xFF;
    return p;
}

struct CountVisitor {
    int n=0;
    inline void OnDomain(const DomainRef&) { ++n; }
};
static volatile long g_sink=0;

int main() {
    auto pkt = build_packet();
    MirpHeader h;
    const size_t N = 5000000;

    {
        CountVisitor cv;
        for(int i=0;i<100000;i++){ ParseMirpHeader(pkt.data(),pkt.size(),h); IterateDomainsT(pkt.data()+24,h.Length,cv); }
        double t0=now_ns();
        for(size_t i=0;i<N;i++){ ParseMirpHeader(pkt.data(),pkt.size(),h); IterateDomainsT(pkt.data()+24,h.Length,cv); }
        double t1=now_ns(); g_sink += cv.n;
        printf("[frame only] %.2f ns/pkt  %.1f Mpps\n",(t1-t0)/N, N/(t1-t0)*1000.0);
    }
    {
        MarketStore store; MarketVisitor mv(store);
        uint8_t b1[112]={0}; double tick=5.0, codec=18000.0; int32_t ino=20;
        std::memcpy(b1+92,&tick,8); std::memcpy(b1+100,&codec,8); std::memcpy(b1+108,&ino,4);
        mv.OnDomain(DomainRef{0x0101,112,b1});
        for(int i=0;i<100000;i++){ ParseMirpHeader(pkt.data(),pkt.size(),h); IterateDomainsT(pkt.data()+24,h.Length,mv); }
        double t0=now_ns();
        for(size_t i=0;i<N;i++){ ParseMirpHeader(pkt.data(),pkt.size(),h); IterateDomainsT(pkt.data()+24,h.Length,mv); }
        double t1=now_ns();
        printf("[full + book] %.2f ns/pkt  %.1f Mpps\n",(t1-t0)/N, N/(t1-t0)*1000.0);
    }
    return 0;
}
