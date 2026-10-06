#pragma once
#include <chrono>
#include <optional>
#include <string>

namespace danmaku {
enum class PlaybackStatus { Unknown, Playing, Paused, Stopped };
struct MediaSessionSnapshot {
    std::string applicationId;
    std::string title;
    PlaybackStatus status{PlaybackStatus::Unknown};
    std::chrono::microseconds position{};
    std::chrono::microseconds duration{};
    std::optional<double> playbackRate;
    std::chrono::steady_clock::time_point sampledAt;
};
} // namespace danmaku
