#pragma once
#include <algorithm>
#include <cstdint>
#include <optional>

// Content-byte admission policy; no allocation, GPU queries or playback clock.
class TextureBudget {
  public:
    static constexpr std::int64_t MiB = 1024 * 1024;
    static constexpr std::int64_t shrinkDelayMs = 10000;
    std::int64_t update(std::int64_t demand, bool automatic, std::int64_t limit, std::int64_t nowMs) {
        limit = std::clamp(limit, 32 * MiB, 1024 * MiB);
        if (!automatic) {
            current_ = limit;
            lowSince_.reset();
            return current_;
        }
        auto target = std::min(64 * MiB, limit);
        // 25% headroom and geometric steps reduce changes during short bursts.
        const auto required = demand + demand / 4;
        while (target < required && target < limit) target = std::min(target * 2, limit);
        if (current_ == 0 || current_ > limit) current_ = target;
        if (target >= current_) {
            current_ = target;
            lowSince_.reset();
        } else {
            if (!lowSince_) lowSince_ = nowMs;
            if (nowMs - *lowSince_ >= shrinkDelayMs) {
                current_ = target;
                lowSince_.reset();
            }
        }
        return current_;
    }
  private:
    std::int64_t current_{};
    std::optional<std::int64_t> lowSince_;
};
