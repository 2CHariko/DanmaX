#pragma once
#include <chrono>

namespace danmaku {
// The caller supplies elapsed monotonic time; no GUI or wall-clock dependency.
class PlaybackClock {
public:
    using Duration = std::chrono::microseconds;
    void setPaused(bool paused) noexcept;
    [[nodiscard]] bool paused() const noexcept;
    void advance(Duration elapsed) noexcept;
    void reset() noexcept;
    [[nodiscard]] Duration position() const noexcept;
private:
    Duration position_{};
    bool paused_{true};
};
}
