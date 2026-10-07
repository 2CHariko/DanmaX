#pragma once
#include <algorithm>
#include <cmath>

namespace danmaku {
// Caller supplies monotonic seconds. Media corrections never become animation deltas.
class MediaClock {
  public:
    bool synchronize(double position, double rate, bool playing, double now, bool reset = false) {
        advance(now);
        const bool discontinuity = reset || !valid_ || position < position_ - 0.25 ||
                                   std::abs(position - position_) > 0.75;
        position_ = std::isfinite(position) ? std::max(0.0, position) : position_;
        rate_ = std::isfinite(rate) && rate > 0 ? rate : 1.0;
        playing_ = playing;
        valid_ = true;
        if (discontinuity) motion_ = 0;
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
    double takeMotion() { const auto result = motion_; motion_ = 0; return result; }
  private:
    double position_{}, rate_{1}, last_{}, motion_{};
    bool valid_{}, playing_{};
};
}
