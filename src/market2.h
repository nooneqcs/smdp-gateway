#pragma once
#include "decode.h"
#include "delta.h"
#include "book.h"
#include <cstdint>
#include <unordered_map>

namespace smdp {

struct ContractState {
    ContractInfo    info{};
    TradeMarketData trade{};
    Book            book{};
};

class MarketStore {
public:
    ContractState& Get(int64_t ino) { return m_[ino]; }
    size_t Size() const { return m_.size(); }
private:
    std::unordered_map<int64_t, ContractState> m_;
};

class MarketVisitor : public DomainVisitor {
public:
    explicit MarketVisitor(MarketStore& s) : store_(s) {}
    void OnDomain(const DomainRef& d) override;
    int64_t Current() const { return cur_ino_; }
private:
    MarketStore& store_;
    int64_t cur_ino_ = -1;
    ContractState* cur_ = nullptr;
};

} // namespace smdp
