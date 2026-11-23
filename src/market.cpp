#include "market.h"

namespace smdp {

void MarketVisitor::OnDomain(const DomainRef& d) {
    switch (static_cast<uint16_t>(d.FieldID)) {
    case 0x0003: {
        ContractDeltaHead h;
        if (DecodeContractHead(d, h) >= 0) cur_ino_ = h.InstrumentNo;
        break;
    }
    case 0x0101: {
        ContractInfo ci;
        if (DecodeContractInfo(d, ci) >= 0) store_.Get(ci.InstrumentNo).info = ci;
        break;
    }
    case 0x0102: {
        TradeMarketData t;
        if (DecodeTradeMarketData(d, t) >= 0) store_.Get(t.InstrumentNo).trade = t;
        break;
    }
    case 0x0103: {
        PriceBookLevel lv;
        if (DecodePriceBookLevel(d, lv)) store_.Get(lv.InstrumentNo).book.LoadSnapshotLevel(lv);
        break;
    }
    case 0x1001: {
        PriceLevelDelta ev;
        if (cur_ino_ >= 0 && DecodePriceLevelDelta(d, ev) >= 0) {
            auto& s = store_.Get(cur_ino_);
            s.book.ApplyDelta(ev, s.info.CodecPrice, s.info.PriceTick);
        }
        break;
    }
    case 0x1002: {
        TradeSummary ts;
        if (cur_ino_ >= 0 && DecodeTradeSummary(d, ts) >= 0) {
            auto& s = store_.Get(cur_ino_);
            s.trade.LastPrice     = s.info.CodecPrice + (double)ts.LastPriceOffset * s.info.PriceTick;
            s.trade.Volume       += (int32_t)ts.VolumeChange;
            s.trade.OpenInterest += (double)ts.OpenInterestChange;
            // Turnover 公式涉及合约乘数，暂略
        }
        break;
    }
    case 0x1011: case 0x1012: case 0x1013: case 0x1014:
    case 0x1015: case 0x1016: case 0x1017: {
        int64_t off;
        if (cur_ino_ >= 0 && DecodeSingleVIntOff(d, off)) {
            auto& s = store_.Get(cur_ino_);
            double price = s.info.CodecPrice + (double)off * s.info.PriceTick;
            switch (static_cast<uint16_t>(d.FieldID)) {
            case 0x1011: s.trade.HighestPrice    = price; break;
            case 0x1012: s.trade.LowestPrice     = price; break;
            case 0x1013: s.trade.OpenPrice       = price; break;
            case 0x1014: s.trade.ClosePrice      = price; break;
            case 0x1015: s.trade.UpperLimitPrice = price; break;
            case 0x1016: s.trade.LowerLimitPrice = price; break;
            case 0x1017: s.trade.SettlementPrice = price; break;
            }
        }
        break;
    }
    case 0x1018: {
        double cd;
        if (cur_ino_ >= 0 && DecodeCurrDelta(d, cd)) store_.Get(cur_ino_).trade.CurrDelta = cd;
        break;
    }
    default: break;   // 未知域靠域头 FieldSize 已跳过
    }
}

} // namespace smdp
