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
    int64_t         last_change_no = 0;
    uint32_t        gaps           = 0;
};

class MarketStore {
public:
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
    explicit MarketVisitor(MarketStore& s) : store_(s) {}
    void OnDomain(const DomainRef& d) override;
    int64_t Current() const { return cur_ino_; }
    auto LastIno() const { return cur_ino_; }
    const std::vector<int64_t>& Touched() const { return touched_; }
    void ClearTouched() { touched_.clear(); }
private:
    MarketStore& store_;
    int64_t cur_ino_ = -1;
    ContractState* cur_ = nullptr;
    std::vector<int64_t> touched_;
};

} // namespace smdp

