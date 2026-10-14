#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace danmaku {
// Absolute monotonic deadlines: skip missed slots without catch-up bursts.
class FramePacer {
  public:
    void reset(std::int64_t now, int limit) {
        period_ = limit > 0 ? static_cast<std::int64_t>(std::llround(1e9 / limit)) : 0;
        deadline_ = now;
    }
    bool due(std::int64_t now) const { return !period_ || now >= deadline_; }
    std::int64_t wait(std::int64_t now) const { return period_ ? std::max<std::int64_t>(0, deadline_ - now) : 0; }
    // Prepare an asynchronous platform request ahead of the render deadline.
    // Rendering itself still uses due(), so a lead never admits an early frame.
    std::int64_t requestWait(std::int64_t now, std::int64_t lead) const {
        return std::max<std::int64_t>(0, wait(now) - std::max<std::int64_t>(0, lead));
    }
    void started(std::int64_t now) {
        if (!period_) { deadline_ = now; return; }
        if (now >= deadline_) deadline_ += ((now - deadline_) / period_ + 1) * period_;
    }
  private:
    std::int64_t period_{}, deadline_{};
};

class AdaptiveFramePolicy {
  public:
    enum class State { Disabled, Inactive, Warming, Monitoring, Draining, Reduced, Unmet };
    void configure(int capacity, double target, bool enabled, double now) {
        capacity_ = std::clamp(capacity, 1, 5000);
        floor_ = (capacity_ + 3) / 4;
        target_ = std::isfinite(target) ? std::max(0.0, target) : 0;
        enabled_ = enabled && target_ > 0;
        reset(now, true);
    }
    void reset(double now, bool restore = false) {
        if (restore) limit_ = capacity_;
        eligible_ = false;
        warmUntil_ = now + 2;
        windowStart_ = -1;
        frames_ = bad_ = good_ = 0;
        frequency_ = 0;
        state_ = enabled_ ? State::Inactive : State::Disabled;
    }
    void observe(double now, bool eligible, std::size_t active, bool presented = false) {
        if (!enabled_) { state_ = State::Disabled; return; }
        if (!eligible) { reset(now); return; }
        if (!eligible_) { eligible_ = true; warmUntil_ = now + 2; state_ = State::Warming; }
        if (now < warmUntil_) return;
        // The next judgement must contain only the drained workload, not a
        // partially drained window with the old, larger live population.
        if (state_ == State::Draining && active <= static_cast<std::size_t>(limit_)) {
            windowStart_ = now;
            frames_ = bad_ = good_ = 0;
            state_ = State::Reduced;
        }
        if (windowStart_ < 0) {
            windowStart_ = now;
            state_ = limit_ < capacity_ ? State::Reduced : State::Monitoring;
        }
        const double elapsed = now - windowStart_;
        if (elapsed >= 1) {
            frequency_ = frames_ / elapsed;
            frames_ = 0;
            windowStart_ = now;
            if (active > static_cast<std::size_t>(limit_)) {
                bad_ = good_ = 0;
                state_ = State::Draining;
            } else if (frequency_ < target_ * .90) {
                good_ = 0;
                if (++bad_ >= 2) {
                    bad_ = 0;
                    if (limit_ == floor_) state_ = State::Unmet;
                    else {
                        limit_ = std::max(floor_, limit_ - std::max(1, (limit_ + 9) / 10));
                        state_ = active > static_cast<std::size_t>(limit_) ? State::Draining : State::Reduced;
                    }
                }
            } else if (frequency_ >= target_ * .97) {
                bad_ = 0;
                if (++good_ >= 5) {
                    good_ = 0;
                    limit_ = std::min(capacity_, limit_ + std::max(1, (capacity_ + 19) / 20));
                }
                state_ = limit_ < capacity_ ? State::Reduced : State::Monitoring;
            } else {
                bad_ = good_ = 0;
                state_ = limit_ < capacity_ ? State::Reduced : State::Monitoring;
            }
        }
        if (presented) ++frames_;
        if (active > static_cast<std::size_t>(limit_)) state_ = State::Draining;
    }
    int limit() const { return limit_; }
    int floor() const { return floor_; }
    double frequency() const { return frequency_; }
    State state() const { return state_; }
  private:
    int capacity_{500}, floor_{125}, limit_{500}, frames_{}, bad_{}, good_{};
    double target_{60}, warmUntil_{}, windowStart_{-1}, frequency_{};
    bool enabled_{}, eligible_{};
    State state_{State::Disabled};
};
} // namespace danmaku
