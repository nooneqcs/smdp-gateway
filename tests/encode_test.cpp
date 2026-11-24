#include "encode.h"
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
using namespace smdp;

static int g_fail=0;
#define CHECK(c) do{ if(!(c)){ printf("FAIL %s:%d %s\n",__FILE__,__LINE__,#c); ++g_fail;} }while(0)

static DomainRef ToDomain(const std::vector<uint8_t>& dom) {
    int16_t fid, fsz;
    std::memcpy(&fid, dom.data(), 2);
    std::memcpy(&fsz, dom.data()+2, 2);
    return DomainRef{fid, fsz, dom.data()+4};
}

int main() {
    { std::vector<uint8_t> b; ContractDeltaHead h{20,3}; EncodeContractHead(b,h);
      ContractDeltaHead r{}; CHECK(DecodeContractHead(ToDomain(b), r) >= 0);
      CHECK(r.InstrumentNo==20 && r.ChangeNo==3); }

    { std::vector<uint8_t> b; PriceLevelDelta e{'2','0',1,0,100000}; EncodePriceLevelDelta(b,e);
      PriceLevelDelta r{}; CHECK(DecodePriceLevelDelta(ToDomain(b), r) >= 0);
      CHECK(r.EventType=='2'&&r.MDEntryType=='0'&&r.PriceLevel==1&&r.Volume==100000);
      printf("0x1001 vol=100000 domain_bytes=%zu\n", b.size()); }

    { std::vector<uint8_t> b; TradeSummary s{5,2,-1,3}; EncodeTradeSummary(b,s);
      TradeSummary r{}; CHECK(DecodeTradeSummary(ToDomain(b), r) >= 0);
      CHECK(r.LastPriceOffset==5&&r.VolumeChange==2&&r.TurnoverOffset==-1&&r.OpenInterestChange==3); }

    { std::vector<uint8_t> b; EncodeCurrDelta(b, 1.5);
      double r=0; CHECK(DecodeCurrDelta(ToDomain(b), r) && r==1.5); }

    { std::vector<uint8_t> body;
      EncodeContractHead(body, ContractDeltaHead{20,3});
      EncodePriceLevelDelta(body, PriceLevelDelta{'2','0',1,0,5});
      EncodeTradeSummary(body, TradeSummary{1,10,0,0});
      auto pkt = PackMirpPacket(0x01, 7, 1001, 7, 1326287735, 11700, 0, body);
      MirpHeader h; CHECK(ParseMirpHeader(pkt.data(), pkt.size(), h)==0);
      CHECK(h.Length==body.size() && h.PacketNo==7 && h.TopicID==1001);
      printf("packet=%zu body=%zu Length=%u\n", pkt.size(), body.size(), h.Length); }

    printf(g_fail?"FAILED: %d\n":"ALL PASS\n", g_fail);
    return g_fail?1:0;
}

