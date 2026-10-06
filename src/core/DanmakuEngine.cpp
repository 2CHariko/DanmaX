#include "core/DanmakuEngine.h"
#include <algorithm>
#include <cmath>

namespace danmaku {
void Engine::load(std::vector<Item> items) {
    items.erase(std::remove_if(items.begin(), items.end(),
                               [](const Item& item) {
                                   return !std::isfinite(item.time) || item.time < 0 || item.text.empty();
                               }),
                items.end());
    const auto byTime = [](const Item& a, const Item& b) { return a.time < b.time; };
    if (!std::is_sorted(items.begin(), items.end(), byTime))
        std::stable_sort(items.begin(), items.end(), byTime);
    items_ = std::move(items);
    dropped_ = 0;
    seek(0);
}
void Engine::configure(Options options, double width, double height) {
    options.speed = std::clamp(options.speed, 10.0, 2000.0);
    options.fixedSeconds = std::clamp(options.fixedSeconds, 0.5, 30.0);
    options.trackHeight = std::max(options.trackHeight, 8.0);
    options.maxTracks = std::clamp(options.maxTracks, 1, 100);
    options.maxActive = std::clamp(options.maxActive, 1, 5000);
    options_ = options;
    width_ = std::max(width, 1.0);
    height_ = std::max(height, 1.0);
    slots_.resize(static_cast<std::size_t>(options.maxActive));
    seek(position_);
}
void Engine::clear() {
    activeCount_ = 0;
    free_.clear();
    free_.reserve(slots_.size());
    for (std::size_t i = slots_.size(); i > 0; --i) {
        slots_[i - 1].alive = false;
        free_.push_back(i - 1);
    }
}
void Engine::seek(double position) {
    clear();
    position_ = lastPosition_ = std::max(0.0, position);
    initialized_ = true;
    cursor_ = static_cast<std::size_t>(
        std::lower_bound(items_.begin(), items_.end(), position_,
                         [](const Item& item, double time) { return item.time < time; }) -
        items_.begin());
}
void Engine::tick(double position, double elapsed, bool playing, const Measure& measure) {
    if (!std::isfinite(position) || !std::isfinite(elapsed))
        return;
    if (!initialized_ || position < lastPosition_ - 0.25 ||
        std::abs(position - lastPosition_ - (playing ? elapsed : 0)) > 1.0)
        seek(position);
    position_ = position;
    if (!playing) {
        lastPosition_ = position;
        return;
    }
    elapsed = std::max(0.0, elapsed);
    for (std::size_t i = 0; i < slots_.size(); ++i) {
        auto& active = slots_[i];
        if (!active.alive)
            continue;
        if (active.mode == Mode::Scroll)
            active.x -= options_.speed * elapsed;
        else
            active.remaining -= elapsed;
        if ((active.mode == Mode::Scroll && active.x + active.width < 0) || active.remaining <= 0) {
            active.alive = false;
            free_.push_back(i);
            --activeCount_;
        }
    }
    while (cursor_ < items_.size() && items_[cursor_].time <= position) {
        // Never burst stale comments after a stalled or hidden window.
        if (items_[cursor_].time >= position - 0.4) {
            if (!spawn(cursor_, measure(items_[cursor_])))
                ++dropped_;
        } else
            ++dropped_;
        ++cursor_;
    }
    lastPosition_ = position;
}
bool Engine::spawn(std::size_t index, double width) {
    if (free_.empty() || width <= 0 || !std::isfinite(width))
        return false;
    const auto mode = items_[index].mode;
    const int tracks = std::min(options_.maxTracks, static_cast<int>(height_ / options_.trackHeight));
    for (int attempt = 0; attempt < tracks; ++attempt) {
        const int lane = static_cast<int>((nextTrack_ + attempt) % tracks);
        const double y =
            mode == Mode::Bottom ? height_ - (lane + 1) * options_.trackHeight : lane * options_.trackHeight;
        bool available = true;
        if (!options_.overlap) {
            for (const auto& other : slots_) {
                if (!other.alive || other.y + other.height <= y || y + options_.trackHeight <= other.y)
                    continue;
                // Fixed and scrolling comments share physical lanes. All scrolls have equal speed.
                if (mode != Mode::Scroll || other.mode != Mode::Scroll ||
                    other.x + other.width + 16 > width_) {
                    available = false;
                    break;
                }
            }
        }
        if (!available)
            continue;
        auto& active = slots_[free_.back()];
        free_.pop_back();
        active = {true,
                  ++sequence_,
                  index,
                  mode,
                  mode == Mode::Scroll ? width_ : (width_ - width) / 2,
                  y,
                  width,
                  options_.trackHeight,
                  mode == Mode::Scroll ? 1e12 : options_.fixedSeconds};
        ++activeCount_;
        nextTrack_ = static_cast<std::size_t>(lane + 1);
        return true;
    }
    return false;
}
} // namespace danmaku
