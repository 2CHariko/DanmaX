#include "core/PlaybackClock.h"

namespace danmaku {
void PlaybackClock::setPaused(bool paused) noexcept {
    paused_ = paused;
}
bool PlaybackClock::paused() const noexcept {
    return paused_;
}
void PlaybackClock::advance(Duration elapsed) noexcept {
    if (!paused_ && elapsed > Duration::zero())
        position_ += elapsed;
}
void PlaybackClock::reset() noexcept {
    position_ = Duration::zero();
}
PlaybackClock::Duration PlaybackClock::position() const noexcept {
    return position_;
}
} // namespace danmaku
