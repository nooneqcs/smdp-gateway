#include "vint.h"
#include "mirp.h"
#include "decode.h"
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
static void roundtrip(int64_t v){
    std::vector<uint8_t> b; put_zz(b,v);
    const uint8_t* p=b.data(); const uint8_t* end=p+b.size();
    int64_t out=0; bool ok=DecodeSVInt(p,end,out);
    CHECK(ok && out==v && p==end);
    if(!(ok&&out==v)) printf("  v=%lld got=%lld ok=%d len=%zu\n",(long long)v,(long long)out,ok,b.size());
}

int main(){
    // A) VInt 多字节/负数/边界值往返
    int64_t vals[]={0,1,-1,63,64,127,128,300,-300,1000,-1000,
                    1LL<<20, -(1LL<<20), 1LL<<40, -(1LL<<40),
                    INT64_MAX, INT64_MIN};
    for(int64_t v: vals) roundtrip(v);

    // B) 截断：续位后没字节
    { uint8_t b[1]={0x80}; const uint8_t*p=b; int64_t o; CHECK(!DecodeSVInt(p,b+1,o)); }
    { uint8_t b[2]={0x80,0x80}; const uint8_t*p=b; int64_t o; CHECK(!DecodeSVInt(p,b+2,o)); }

    // C) 畸形：>10 字节
    { uint8_t b[11]; std::memset(b,0x80,11); const uint8_t*p=b; int64_t o; CHECK(!DecodeSVInt(p,b+11,o)); }

    // D) 域超长：FieldSize > 字段和，多余应丢弃、不越读
    {
        std::vector<uint8_t> body; put_zz(body,20); put_zz(body,3);
        while(body.size()<10) body.push_back(0x00);
        DomainRef d{0x0003,10,body.data()};
        ContractDeltaHead h; int used=DecodeContractHead(d,h);
        CHECK(used>0 && used<10);
        CHECK(h.InstrumentNo==20 && h.ChangeNo==3);
        printf("D 超长域: used=%d/%d ino=%lld chg=%lld\n",used,d.FieldSize,(long long)h.InstrumentNo,(long long)h.ChangeNo);
    }

    // E) 域越界：声明 FieldSize 超 body → IterateDomains 返回 -1
    {
        uint8_t body[8]; int16_t fid=0x1001, fsz=100;
        std::memcpy(body,&fid,2); std::memcpy(body+2,&fsz,2); std::memset(body+4,0,4);
        struct V: DomainVisitor { void OnDomain(const DomainRef&) override {} } v;
        CHECK(IterateDomains(body,8,v)==-1);
    }

    // F) 头长度不足
    { uint8_t b[10]={0}; MirpHeader h; CHECK(ParseMirpHeader(b,10,h)==-1); }

    printf(g_fail?"FAILED: %d\n":"ALL PASS\n", g_fail);
    return g_fail?1:0;
}
