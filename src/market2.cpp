#include "market2.h"

namespace smdp {

void MarketVisitor::OnDomain(const DomainRef& d) {
    switch (static_cast<uint16_t>(d.FieldID)) {
    case 0x0003: {
        ContractDeltaHead h;
        if (DecodeContractHead(d, h) >= 0 && (!cur_ || h.InstrumentNo != cur_ino_)) {
            cur_ino_ = h.InstrumentNo;
            cur_ = &store_.Get(cur_ino_);
        }
        break;
    }
    case 0x0101: { ContractInfo ci;    if (DecodeContractInfo(d, ci) >= 0)    store_.Get(ci.InstrumentNo).info = ci; break; }
    case 0x0102: { TradeMarketData t;  if (DecodeTradeMarketData(d, t) >= 0)  store_.Get(t.InstrumentNo).trade = t; break; }
    case 0x0103: { PriceBookLevel lv;  if (DecodePriceBookLevel(d, lv))       store_.Get(lv.InstrumentNo).book.LoadSnapshotLevel(lv); break; }
    case 0x1001: {
        PriceLevelDelta ev;
        if (cur_ && DecodePriceLevelDelta(d, ev) >= 0)
            cur_->book.ApplyDelta(ev, cur_->info.CodecPrice, cur_->info.PriceTick);
        break;
    }
    case 0x1002: {
        TradeSummary ts;
        if (cur_ && DecodeTradeSummary(d, ts) >= 0) {
            cur_->trade.LastPrice     = cur_->info.CodecPrice + (double)ts.LastPriceOffset * cur_->info.PriceTick;
            cur_->trade.Volume       += (int32_t)ts.VolumeChange;
            cur_->trade.OpenInterest += (double)ts.OpenInterestChange;
        }
        break;
    }
    case 0x1011: case 0x1012: case 0x1013: case 0x1014:
    case 0x1015: case 0x1016: case 0x1017: {
        int64_t off;
        if (cur_ && DecodeSingleVIntOff(d, off)) {
            double price = cur_->info.CodecPrice + (double)off * cur_->info.PriceTick;
            switch (static_cast<uint16_t>(d.FieldID)) {
            case 0x1011: cur_->trade.HighestPrice    = price; break;
            case 0x1012: cur_->trade.LowestPrice     = price; break;
            case 0x1013: cur_->trade.OpenPrice       = price; break;
            case 0x1014: cur_->trade.ClosePrice      = price; break;
            case 0x1015: cur_->trade.UpperLimitPrice = price; break;
            case 0x1016: cur_->trade.LowerLimitPrice = price; break;
            case 0x1017: cur_->trade.SettlementPrice = price; break;
            }
        }
        break;
    }
    case 0x1018: {
        double cd;
        if (cur_ && DecodeCurrDelta(d, cd)) cur_->trade.CurrDelta = cd;
        break;
    }
    default: break;
    }
}

} // namespace smdp
