#include "core/DanmakuEngine.h"
#include <algorithm>
#include <cmath>

namespace danmaku {
namespace {
// Font metrics produce fractional lane heights. Snap numerical noise at integer
// boundaries before floor/ceil so admission and overlap use identical lanes.
double snapLaneRatio(double ratio) {
    const double integer = std::round(ratio);
    return std::abs(ratio - integer) < 1e-9 ? integer : ratio;
}
}
int Engine::fittingLanes(Mode mode, double height) const {
    const double ratio = snapLaneRatio(height_ / options_.trackHeight);
    // Scrolling comments may use the partial final lane; the renderer clips it.
    // Fixed comments retain their full-rectangle, screen-edge anchoring rules.
    const int tracks = std::min(options_.maxTracks, static_cast<int>(
        mode == Mode::Scroll ? std::ceil(ratio) : std::floor(ratio)));
    const double remaining = snapLaneRatio(tracks - height / options_.trackHeight);
    if (remaining < 0) return 0;
    return static_cast<int>(std::floor(remaining)) + 1;
}
double Engine::laneY(Mode mode, int lane, double height) const {
    return mode == Mode::Bottom ? std::max(0.0, height_ - lane * options_.trackHeight - height)
                                : lane * options_.trackHeight;
}
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
    retiredBySettings_ = 0;
    suppressedWhileHidden_ = 0;
    seek(0);
    if (!items_.empty()) reconfigure(options_, width_, height_);
}
void Engine::configure(Options options, double width, double height) {
    clear();
    reconfigure(options, width, height);
    seek(position_);
}
void Engine::reconfigure(Options options, double width, double height, const Measure& measure) {
    options.speed = std::clamp(options.speed, 10.0, 2000.0);
    options.fixedSeconds = std::clamp(options.fixedSeconds, 0.5, 30.0);
    options.trackHeight = std::max(options.trackHeight, 8.0);
    options.maxTracks = std::clamp(options.maxTracks, 1, 100);
    options.maxActive = std::clamp(options.maxActive, 1, 5000);
    const auto oldOptions = options_;
    const auto oldHeight = height_;
    const bool reflow = measure || options.trackHeight != options_.trackHeight ||
                        options.maxTracks != options_.maxTracks || width != width_ || height != height_ ||
                        (options_.overlap && !options.overlap);
    options_ = options;
    width_ = std::max(width, 1.0);
    height_ = std::max(height, 1.0);
    // Settings may change while no file is loaded. Allocate only on the next load.
    if (items_.empty()) return;
    if (slots_.size() != static_cast<std::size_t>(options.maxActive)) {
        // Compact by arrival order before shrinking; high-numbered live slots must not vanish.
        std::vector<Active> resized(static_cast<std::size_t>(options.maxActive));
        std::size_t count = 0;
        for (auto slot : active_) {
            if (count < resized.size()) resized[count++] = slots_[slot];
            else ++retiredBySettings_;
        }
        slots_ = std::move(resized);
        active_.resize(count);
        for (std::size_t i = 0; i < count; ++i) active_[i] = i;
    }
    active_.reserve(slots_.size());
    occupancy_.resize(static_cast<std::size_t>(std::ceil(height_ / options_.trackHeight)) + 1);
    for (auto& bucket : occupancy_) bucket.clear();
    std::size_t retained = 0;
    for (auto slot : active_) {
        auto& a = slots_[slot];
        if (a.mode != Mode::Scroll) a.remaining = options_.fixedSeconds - a.age;
        bool keep = a.remaining > 0;
        double measuredHeight = a.height;
        if (keep && measure) {
            const auto extent = measure(items_[a.item]);
            keep = std::isfinite(extent.width) && extent.width > 0 &&
                   std::isfinite(extent.height) && extent.height >= 0;
            if (keep) {
                a.width = extent.width;
                measuredHeight = extent.height;
            }
        }
        if (keep && reflow) {
            const double edge = a.mode == Mode::Bottom ? oldHeight - a.y - a.height : a.y;
            const int lane = std::max(0, static_cast<int>(std::round(edge / oldOptions.trackHeight)));
            a.height = std::max(options_.trackHeight, measuredHeight);
            if (a.mode != Mode::Scroll) a.x = (width_ - a.width) / 2;
            const int fitting = fittingLanes(a.mode, a.height);
            const auto place = [&](int candidateLane) {
                if (candidateLane < 0 || candidateLane >= fitting) return false;
                a.y = laneY(a.mode, candidateLane, a.height);
                return fits(a);
            };
            keep = place(lane);
            for (int attempt = 0; !keep && attempt < fitting; ++attempt) keep = place(attempt);
            if (!keep && options_.overlap && fitting > 0) {
                a.y = laneY(a.mode, std::clamp(lane, 0, fitting - 1), a.height);
                keep = true;
            }
        }
        if (keep && a.mode == Mode::Scroll && a.x + a.width < 0) keep = false;
        if (keep) {
            active_[retained++] = slot;
            indexSlot(slot);
        } else {
            a.alive = false;
            ++retiredBySettings_;
        }
    }
    active_.resize(retained);
    activeCount_ = retained;
    free_.clear();
    free_.reserve(slots_.size());
    for (std::size_t i = slots_.size(); i > 0; --i)
        if (!slots_[i - 1].alive) free_.push_back(i - 1);
}
void Engine::clear() {
    activeCount_ = 0;
    overlapTrack_ = 0;
    active_.clear();
    for (auto& bucket : occupancy_) bucket.clear();
    free_.clear();
    free_.reserve(slots_.size());
    for (std::size_t i = slots_.size(); i > 0; --i) {
        slots_[i - 1].alive = false;
        free_.push_back(i - 1);
    }
}
void Engine::unload() {
    std::vector<Item>().swap(items_);
    std::vector<Active>().swap(slots_);
    std::vector<std::size_t>().swap(free_);
    std::vector<std::size_t>().swap(active_);
    std::vector<std::vector<std::size_t>>().swap(occupancy_);
    position_ = lastPosition_ = 0;
    initialized_ = false;
    cursor_ = activeCount_ = overlapTrack_ = 0;
    dropped_ = retiredBySettings_ = suppressedWhileHidden_ = 0;
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
void Engine::tick(double position, double elapsed, bool playing, const Measure& measure, bool emitNew) {
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
    std::size_t retained = 0;
    for (const auto i : active_) {
        auto& active = slots_[i];
        if (!active.alive)
            continue;
        active.age += elapsed;
        if (active.mode == Mode::Scroll)
            active.x -= options_.speed * elapsed;
        else
            active.remaining -= elapsed;
        if ((active.mode == Mode::Scroll && active.x + active.width < 0) || active.remaining <= 0) {
            active.alive = false;
            free_.push_back(i);
            --activeCount_;
        } else active_[retained++] = i;
    }
    active_.resize(retained);
    rebuildOccupancy();
    while (cursor_ < items_.size() && items_[cursor_].time <= position) {
        // Hidden maintenance advances existing objects without measuring/spawning new text.
        if (!emitNew)
            ++suppressedWhileHidden_;
        else if (items_[cursor_].time >= position - 0.4) {
            if (!spawn(cursor_, measure(items_[cursor_])))
                ++dropped_;
        } else
            ++dropped_;
        ++cursor_;
    }
    lastPosition_ = position;
}
void Engine::indexSlot(std::size_t slot) {
    const auto& a = slots_[slot];
    const auto first = static_cast<std::size_t>(std::floor(a.y / options_.trackHeight));
    const auto last = static_cast<std::size_t>(std::ceil((a.y + a.height) / options_.trackHeight));
    for (auto bucket = first; bucket < last && bucket < occupancy_.size(); ++bucket)
        occupancy_[bucket].push_back(slot);
}
void Engine::rebuildOccupancy() {
    for (auto& bucket : occupancy_) bucket.clear();
    for (auto slot : active_) indexSlot(slot);
}
bool Engine::fits(const Active& candidate) const {
    const auto first = static_cast<std::size_t>(std::floor(candidate.y / options_.trackHeight));
    const auto last = static_cast<std::size_t>(std::ceil((candidate.y + candidate.height) / options_.trackHeight));
    for (auto bucket = first; bucket < last && bucket < occupancy_.size(); ++bucket) {
        for (auto slot : occupancy_[bucket]) {
            const auto& other = slots_[slot];
            // Adjacent fractional lanes can overlap by floating-point noise only.
            if (other.y + other.height <= candidate.y + 1e-7 ||
                candidate.y + candidate.height <= other.y + 1e-7) continue;
            if (candidate.mode != Mode::Scroll || other.mode != Mode::Scroll ||
                !(candidate.x + candidate.width + 16 <= other.x || other.x + other.width + 16 <= candidate.x))
                return false;
        }
    }
    return true;
}
bool Engine::spawn(std::size_t index, Extent extent) {
    const double width = extent.width;
    if (free_.empty() || width <= 0 || !std::isfinite(width) || !std::isfinite(extent.height) || extent.height < 0)
        return false;
    const double height = std::max(options_.trackHeight, extent.height);
    const auto mode = items_[index].mode;
    const int fitting = fittingLanes(mode, height);
    int selected = -1;
    for (int attempt = 0; attempt < fitting; ++attempt) {
        const int lane = attempt;
        const double y = laneY(mode, lane, height);
        Active candidate{};
        candidate.mode = mode;
        candidate.x = mode == Mode::Scroll ? width_ : (width_ - width) / 2;
        candidate.y = y;
        candidate.width = width;
        candidate.height = height;
        if (!fits(candidate))
            continue;
        selected = lane;
        break;
    }
    // Overlap is a fallback after checking safe lanes. Cycling every arrival
    // through the screen produces diagonal staircases even at low density.
    if (selected < 0 && options_.overlap && fitting > 0)
        selected = static_cast<int>(overlapTrack_++ % static_cast<std::size_t>(fitting));
    if (selected >= 0) {
        const double y = laneY(mode, selected, height);
        const auto slot = free_.back();
        auto& active = slots_[slot];
        free_.pop_back();
        active = {true,
                  ++sequence_,
                  index,
                  mode,
                  mode == Mode::Scroll ? width_ : (width_ - width) / 2,
                  y,
                  width,
                  height,
                  mode == Mode::Scroll ? 1e12 : options_.fixedSeconds};
        ++activeCount_;
        active_.push_back(slot);
        indexSlot(slot);
        return true;
    }
    return false;
}
} // namespace danmaku
