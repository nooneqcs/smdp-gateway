// 行情 → 策略 最新值快照（seqlock，单写多读）
// 语义：读到的永远是"某一时刻完整的 tick"，不会撕裂；写端永不被读端阻塞。
// 实现：payload 逐 8 字节 atomic + seq acquire/release —— 所有共享访问都是原子的，
//       严格 C++ 内存模型下无 data race（不是靠 memcpy 硬撑的那种 seqlock）。
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace qgw {

template <typename T>
struct alignas(64) QuoteSnapshot {
    static_assert(std::is_trivially_copyable<T>::value, "T must be trivially copyable");
    static constexpr size_t WORDS = (sizeof(T) + 7) / 8;

    std::atomic<uint32_t> seq{0};
    std::atomic<uint64_t> words[WORDS];
    alignas(64) std::atomic<uint64_t> publishes{0};   // 诊断计数

    // 写端：唯一写者（行情核）
    void publish(const T& t) noexcept {
        uint64_t tmp[WORDS] = {};
        std::memcpy(tmp, &t, sizeof(T));
        uint32_t s = seq.load(std::memory_order_relaxed);
        seq.store(s + 1, std::memory_order_relaxed);           // 奇 = 写入中
        std::atomic_thread_fence(std::memory_order_release);
        for (size_t i = 0; i < WORDS; ++i)
            words[i].store(tmp[i], std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        seq.store(s + 2, std::memory_order_release);           // 偶 = 写完
    }

    // 读端：任意读者（策略核，可多个）。false = 正在写/重试超限，调用方跳过这一拍。
    bool latest(T& out) const noexcept {
        uint64_t tmp[WORDS] = {};
        for (int a = 0; a < 64; ++a) {
            uint32_t s1 = seq.load(std::memory_order_acquire);
            if (s1 & 1u) continue;                             // 写入中
            std::atomic_thread_fence(std::memory_order_acquire);
            for (size_t i = 0; i < WORDS; ++i)
                tmp[i] = words[i].load(std::memory_order_relaxed);
            std::atomic_thread_fence(std::memory_order_acquire);
            uint32_t s2 = seq.load(std::memory_order_acquire);
            if (s1 == s2) { std::memcpy(&out, tmp, sizeof(T)); return s1 != 0; }
        }
        return false;
    }

    bool has_data() const noexcept {
        return seq.load(std::memory_order_acquire) != 0;
    }
};

} // namespace qgw

