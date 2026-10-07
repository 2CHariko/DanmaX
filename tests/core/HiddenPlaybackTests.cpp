#include "core/DanmakuEngine.h"
#include <cmath>
#include <iostream>
using namespace danmaku;
int main() {
    int failed = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { ++failed; std::cerr << message << '\n'; }
    };
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-8; };
    int measured = 0;
    const auto measure = [&](const Item&) { ++measured; return Engine::Extent(100, 40); };
    const auto live = [](const Engine& e, std::size_t i) { return e.activeSlots()[e.activeIndices()[i]]; };
    Options options;
    options.speed = 100; options.trackHeight = 40; options.maxTracks = 5; options.fixedSeconds = 2;
    Engine engine;
    engine.configure(options, 800, 300);
    engine.load({{0, Mode::Scroll, "scroll"}, {0, Mode::Top, "fixed"},
                 {0.2, Mode::Scroll, "hidden"}, {0.6, Mode::Top, "hidden fixed"},
                 {1.5, Mode::Scroll, "future"}});
    engine.tick(0, 0, true, measure);
    const auto scroll = live(engine, 0);
    const auto fixed = live(engine, 1);
    engine.tick(0.5, 0.5, true, measure, false);
    check(engine.activeCount() == 2 && live(engine, 0).id == scroll.id && near(live(engine, 0).x, 750) &&
              live(engine, 1).id == fixed.id && near(live(engine, 1).remaining, 1.5),
          "Hidden playback preserves IDs and advances existing motion/lifetime");
    check(measured == 2 && engine.suppressedWhileHidden() == 1 && engine.dropped() == 0,
          "Hidden records advance the cursor without text measurement or allocation");
    engine.tick(0.5, 100, false, measure, false);
    check(near(live(engine, 0).x, 750) && near(live(engine, 1).remaining, 1.5),
          "Hidden pause freezes positions and lifetimes regardless of wall time");
    engine.tick(1, 0.5, true, measure, false);
    engine.tick(1, 0, true, measure);
    check(engine.activeCount() == 2 && near(live(engine, 0).x, 700) && live(engine, 0).id == scroll.id &&
              engine.suppressedWhileHidden() == 2 && measured == 2,
          "Reappearance neither restarts existing objects nor bursts hidden comments");
    engine.tick(1.5, 0.5, true, measure);
    check(engine.activeCount() == 3 && live(engine, 2).item == 4 && measured == 3,
          "Future comments are scheduled normally after reappearance");
    engine.tick(3, 1.5, true, measure, false);
    check(engine.activeCount() == 2 && live(engine, 0).id == scroll.id,
          "Fixed comments expire naturally while hidden without clearing scrolling comments");
    engine.tick(12, 9, true, measure, false);
    check(engine.activeCount() == 0 && engine.finished(), "Long hidden playback releases expired slots and finishes");
    engine.tick(12, 0, true, measure);
    check(engine.activeCount() == 0, "Long hiding never resurrects expired or suppressed text");

    engine.load({{0, Mode::Scroll, "before seek"}, {10, Mode::Scroll, "at hidden seek"},
                 {10.2, Mode::Scroll, "future"}});
    engine.tick(0, 0, true, measure);
    engine.tick(10, 0, true, measure, false);
    check(engine.activeCount() == 0 && engine.suppressedWhileHidden() == 1,
          "A genuine hidden seek still clears old comments and suppresses the hidden timestamp");
    engine.tick(10.2, 0.2, true, measure);
    check(engine.activeCount() == 1 && live(engine, 0).item == 2,
          "A hidden seek resumes its future cursor without stale playback");
    engine.load({{0, Mode::Top, "new media"}});
    engine.tick(0, 100, false, measure, false);
    check(engine.activeCount() == 0 && engine.suppressedWhileHidden() == 0,
          "Media switch while hidden and paused does not emit a comment");
    engine.tick(0, 0, true, measure);
    check(engine.activeCount() == 1, "Playback after media switch admits the new media's first comment");
    return failed ? 1 : 0;
}
