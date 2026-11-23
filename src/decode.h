#pragma once
#include "mirp.h"
#include <cstdint>

namespace smdp {

// ---- MIRP 增量域 ----
struct ContractDeltaHead { int64_t InstrumentNo; int64_t ChangeNo; };
struct PriceLevelDelta {
    char EventType; char MDEntryType;
    int64_t PriceLevel; int64_t PriceOffset; int64_t Volume;
};
int DecodeContractHead(const DomainRef& d, ContractDeltaHead& out);     // 0x0003
int DecodePriceLevelDelta(const DomainRef& d, PriceLevelDelta& out);   // 0x1001

// ---- MDQP 快照域（定长小端） ----
#pragma pack(push, 1)
struct ContractInfo {                 // 0x0101，112B
    char    InstrumentID[31];
    char    UnderlyingInstrID[31];
    char    ProductClass;
    double  StrikePrice;
    char    OptionsType;
    int32_t VolumeMultiple;
    double  UnderlyingMultiple;
    int32_t IsTrading;
    char    CurrencyID[4];
    double  PriceTick;
    double  CodecPrice;
    int32_t InstrumentNo;
};
struct TradeMarketData {              // 0x0102，154B
    int32_t InstrumentNo;
    double  LastPrice;
    int32_t Volume;
    double  Turnover;
    double  OpenInterest;
    double  HighestPrice;
    double  LowestPrice;
    double  OpenPrice;
    double  ClosePrice;
    double  SettlementPrice;
    double  UpperLimitPrice;
    double  LowerLimitPrice;
    double  PreSettlementPrice;
    double  PreClosePrice;
    double  PreOpenInterest;
    double  PreDelta;
    double  CurrDelta;
    char    ActionDay[9];
    char    UpdateTime[9];
    int32_t UpdateMilliSec;
    int32_t ChangeNo;
};
#pragma pack(pop)
static_assert(sizeof(ContractInfo) == 112, "ContractInfo must be 112");
static_assert(sizeof(TradeMarketData) == 154, "TradeMarketData must be 154");

int DecodeContractInfo(const DomainRef& d, ContractInfo& out);          // 0x0101
int DecodeTradeMarketData(const DomainRef& d, TradeMarketData& out);    // 0x0102

} // namespace smdp
