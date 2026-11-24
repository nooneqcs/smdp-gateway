#pragma once
#include "decode.h"
#include "delta.h"
#include "book.h"
#include <cstdint>
#include <vector>

namespace smdp {

struct ContractState {
    ContractInfo    info{};
    TradeMarketData trade{};
    Book            book{};
};

class MarketStore {
public:
    // 直接按 InstrumentNo 索引（主题内合约编码通常连续 0..M-1）
    ContractState& Get(int64_t ino) {
        size_t i = (ino < 0) ? 0u : static_cast<size_t>(ino);
        if (i >= table_.size()) table_.resize(i + 1);
        return table_[i];
    }
    size_t Size() const { return table_.size(); }
private:
    std::vector<ContractState> table_;
};

class MarketVisitor final : public DomainVisitor {
public:
    auto LastIno() const { return cur_ino_; }
    explicit MarketVisitor(MarketStore& s) : store_(s) {}
    void OnDomain(const DomainRef& d) override;
    int64_t Current() const { return cur_ino_; }
private:
    MarketStore& store_;
    int64_t cur_ino_ = -1;
    ContractState* cur_ = nullptr;
};

} // namespace smdp

