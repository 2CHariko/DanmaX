#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace danmaku {
enum class Mode : int { Scroll = 1, Bottom = 4, Top = 5 };
struct Item {
    double time{};
    Mode mode{Mode::Scroll};
    std::string text;
    std::uint32_t color{0xffffff};
};
struct Options {
    double speed{180}, fixedSeconds{5}, trackHeight{36};
    int maxTracks{18}, maxActive{500};
    bool overlap{};
};
struct Active {
    bool alive{};
    std::uint64_t id{};
    std::size_t item{};
    Mode mode{};
    double x{}, y{}, width{}, height{}, remaining{};
};
class Engine {
  public:
    using Measure = std::function<double(const Item&)>;
    void load(std::vector<Item> items);
    void configure(Options options, double width, double height);
    void seek(double position);
    void clear();
    void tick(double position, double elapsed, bool playing, const Measure& measure);
    [[nodiscard]] const std::vector<Item>& items() const {
        return items_;
    }
    [[nodiscard]] const std::vector<Active>& activeSlots() const {
        return slots_;
    }
    [[nodiscard]] std::size_t activeCount() const {
        return activeCount_;
    }
    [[nodiscard]] bool finished() const {
        return cursor_ >= items_.size() && activeCount_ == 0;
    }
    [[nodiscard]] std::uint64_t dropped() const {
        return dropped_;
    }
    [[nodiscard]] double position() const {
        return position_;
    }

  private:
    bool spawn(std::size_t index, double width);
    std::vector<Item> items_;
    std::vector<Active> slots_;
    std::vector<std::size_t> free_;
    Options options_;
    double width_{1920}, height_{1080}, position_{}, lastPosition_{};
    bool initialized_{};
    std::size_t cursor_{}, activeCount_{}, nextTrack_{};
    std::uint64_t sequence_{}, dropped_{};
};
} // namespace danmaku
