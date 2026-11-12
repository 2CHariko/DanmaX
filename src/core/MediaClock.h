#pragma once
#include <algorithm>
#include <cmath>

namespace danmaku {
// Caller supplies monotonic seconds. Media corrections never become animation deltas.
class MediaClock {
  public:
    bool synchronize(double position, double rate, bool playing, double observedAt, double receivedAt,
                     bool reset = false) {
        if (!std::isfinite(position) || !std::isfinite(observedAt) || !std::isfinite(receivedAt) ||
            observedAt > receivedAt || (!reset && valid_ && observedAt < sampleAt_))
            return false;
        const double predicted = samplePosition_ + (playing_ ? (observedAt - sampleAt_) * rate_ : 0);
        const bool discontinuity = reset || !valid_ || position < predicted - 0.25 ||
                                   std::abs(position - predicted) > 0.75;
        rate = std::isfinite(rate) && rate > 0 ? rate : 1.0;
        if (discontinuity) {
            // Start the reset at the raw observed position (including time zero).
            // The next advance accounts for time since acquisition.
            last_ = consumedThrough_ = observedAt;
            motion_ = 0;
            position_ = std::max(0.0, position);
        } else {
            receivedAt = std::max(receivedAt, last_);
            advance(receivedAt);
            // Replace only the part of the old prediction that hasn't already
            // been rendered. A delayed rate or pause update cannot undo frames.
            const double unconsumedTail = receivedAt - std::max(observedAt, consumedThrough_);
            motion_ = std::max(0.0, motion_ - (playing_ ? unconsumedTail * rate_ : 0)) +
                      (playing ? unconsumedTail * rate : 0);
            position_ = std::max(0.0, position) + (playing ? (receivedAt - observedAt) * rate : 0);
        }
        sampleAt_ = observedAt;
        samplePosition_ = std::max(0.0, position);
        rate_ = rate;
        playing_ = playing;
        valid_ = true;
        return discontinuity;
    }
    void freeze(double now) { advance(now); playing_ = false; }
    void advance(double now) {
        if (!std::isfinite(now) || now < last_) return;
        if (valid_ && playing_) {
            const auto delta = (now - last_) * rate_;
            position_ += delta;
            motion_ += delta;
        }
        last_ = now;
    }
    double position() const { return position_; }
    double takeMotion() { const auto result = motion_; motion_ = 0; consumedThrough_ = last_; return result; }
  private:
    double position_{}, rate_{1}, last_{}, motion_{};
    double sampleAt_{}, samplePosition_{}, consumedThrough_{};
    bool valid_{}, playing_{};
};
}
