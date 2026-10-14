#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

// The presentation callback only writes timestamps. No GUI objects or policy
// evaluation on the render thread, and no queued GUI event for every frame.
class FrameFeedback {
  public:
    struct Sample { std::int64_t at; unsigned flags; };
    static constexpr std::size_t capacity = 4096;
    void setActivity(bool continuous, bool eligible) {
        flags_.store((continuous ? 1u : 0u) | (eligible ? 2u : 0u), std::memory_order_relaxed);
    }
    void record(std::int64_t now) {
        std::lock_guard lock(mutex_);
        total_.fetch_add(1, std::memory_order_relaxed);
        if (size_ == capacity) {
            head_ = (head_ + 1) % capacity;
            --size_;
            lost_.fetch_add(1, std::memory_order_relaxed);
        }
        samples_[(head_ + size_++) % capacity] = {now, flags_.load(std::memory_order_relaxed)};
    }
    void take(std::vector<Sample>& target) {
        target.clear();
        std::lock_guard lock(mutex_);
        for (std::size_t i = 0; i < size_; ++i) target.push_back(samples_[(head_ + i) % capacity]);
        head_ = size_ = 0;
    }
    void discard() { std::lock_guard lock(mutex_); head_ = size_ = 0; }
    std::uint64_t total() const { return total_.load(std::memory_order_relaxed); }
    std::uint64_t lost() const { return lost_.load(std::memory_order_relaxed); }
  private:
    std::array<Sample, capacity> samples_{};
    std::mutex mutex_;
    std::size_t head_{}, size_{};
    std::atomic<unsigned> flags_{};
    std::atomic<std::uint64_t> total_{}, lost_{};
};
