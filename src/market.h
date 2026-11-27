#pragma once
#include "decode.h"
#include "delta.h"
#include "book.h"
#include <cstdint>
#include <unordered_map>

namespace smdp {

struct ContractState {
    ContractInfo    info{};      // 含 CodecPrice / PriceTick
    TradeMarketData trade{};
    Book            book{};
    int64_t         last_change_no = 0;
    uint32_t        gaps           = 0;
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
};

} // namespace smdp

