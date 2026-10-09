#include "core/DanmakuEngine.h"
#include <iostream>
#include <limits>
#include <cmath>
using namespace danmaku;
int main() {
    int failures = 0;
    auto check = [&](bool v, const char* message) {
        if (!v) {
            std::cerr << message << '\n';
            ++failures;
        }
    };
    const auto width = [](const Item&) { return 100.0; };
    Engine engine;
    Options o;
    o.maxTracks = 1;
    o.trackHeight = 40;
    o.speed = 100;
    o.maxActive = 50;
    engine.configure(o, 800, 40);
    engine.load({{0, Mode::Scroll, "first", 0xffffff},
                 {0.5, Mode::Scroll, "overlap", 0xffffff},
                 {1.2, Mode::Scroll, "spaced", 0xffffff}});
    engine.tick(0, 0, true, width);
    check(engine.activeCount() == 1, "First comment must appear at zero");
    engine.tick(0.5, 0.5, true, width);
    check(engine.activeCount() == 1 && engine.dropped() == 1, "Do not release scrolling lane early");
    engine.tick(1.2, 0.7, true, width);
    check(engine.activeCount() == 2, "Release lane after tail and gap enter viewport");
    engine.seek(0);
    check(engine.activeCount() == 0, "Seek clears pool and lanes");
    engine.tick(0, 0, true, width);
    check(engine.activeCount() == 1, "Seek allows immediate reallocation");
    engine.configure(o, 800, 40);
    engine.load({{0, Mode::Top, "fixed", 0xffffff}, {0, Mode::Scroll, "scroll", 0xffffff}});
    engine.tick(0, 0, true, width);
    check(engine.activeCount() == 1, "Fixed and scrolling comments share physical lanes");
    engine.tick(0, 100, false, width);
    check(engine.activeCount() == 1, "Pause freezes fixed comment lifetime");
    engine.tick(0.1, 0.1, true, width);
    check(engine.activeCount() == 1, "Resume excludes paused wall time");
    engine.seek(5);
    engine.tick(5, 0, true, width);
    check(engine.activeCount() == 0, "Seek does not burst old comments");
    engine.seek(0);
    engine.tick(0, 0, true, width);
    check(engine.activeCount() == 1, "Backward seek replays comments");
    o.overlap = true;
    o.maxActive = 2;
    engine.configure(o, 800, 40);
    engine.load({{0, Mode::Scroll, "a", 0}, {0, Mode::Scroll, "b", 0}, {0, Mode::Scroll, "c", 0}});
    engine.tick(0, 0, true, width);
    check(engine.activeCount() == 2 && engine.dropped() == 1, "Pool bound and dropped statistics");
    engine.tick(10, 0, true, width);
    check(engine.activeCount() == 0, "Large jump clears active comments");
    engine.load({{std::numeric_limits<double>::quiet_NaN(), Mode::Scroll, "bad", 0},
                 {2, Mode::Scroll, "b", 0},
                 {1, Mode::Scroll, "a", 0}});
    check(engine.items().size() == 2 && engine.items()[0].time == 1, "Reject nonfinite timestamps and sort");
    o.speed = 30;
    engine.configure(o, 2560, 40);
    engine.load({{0, Mode::Scroll, "slow", 0xffffff}});
    engine.tick(0, 0, true, width);
    engine.tick(16, 16, true, width);
    check(!engine.finished(), "Do not truncate slow comments at an arbitrary tail duration");
    engine.tick(100, 84, true, width);
    check(engine.finished(), "Finish only after the last active comment leaves");
    o = {};
    o.trackHeight = 40;
    o.maxTracks = 1;
    engine.configure(o, 800, 60);
    engine.load({{0, Mode::Bottom, "bottom", 0}, {0, Mode::Top, "top", 0}});
    engine.tick(0, 0, true, width);
    check(engine.activeCount() == 1, "Partial physical lane overlap blocks opposite fixed types");
    check(engine.activeIndices().size() == engine.activeCount(), "Compact active index matches count");
    engine.tick(6, 6, true, width);
    check(engine.activeIndices().empty(), "Expired indices removed");
    engine.seek(0);
    engine.tick(0, 0, true, width);
    check(engine.activeIndices().size() == 1, "Seek resets occupancy and reuses slots");
    engine.configure(o, 800, 80);
    engine.seek(0);
    engine.tick(0, 0, true, width);
    check(engine.activeCount() == 2, "Touching physical lanes do not collide");
    for (const bool overlap : {false, true}) {
        o = {};
        o.speed = 100;
        o.trackHeight = 40;
        o.maxTracks = 4;
        o.overlap = overlap;
        engine.configure(o, 800, 200);
        engine.load({{0, Mode::Scroll, "first", 0}, {0.5, Mode::Scroll, "second", 0},
                     {1.2, Mode::Scroll, "reuse", 0}});
        engine.tick(0, 0, true, width);
        engine.tick(0.5, 0.5, true, width);
        check(engine.activeSlots()[engine.activeIndices().back()].y == 40,
              "Use next lane while the first scrolling tail still blocks entry");
        engine.tick(1.2, 0.7, true, width);
        check(engine.activeCount() == 3 && engine.activeSlots()[engine.activeIndices().back()].y == 0,
              "Reuse the first safe lane instead of forming a diagonal staircase");
        const auto before = engine.activeSlots()[engine.activeIndices().back()];
        engine.tick(1.2, 5, false, width);
        const auto paused = engine.activeSlots()[engine.activeIndices().back()];
        check(before.x == paused.x && before.y == paused.y, "Pause freezes reused lanes");
        engine.seek(0);
        engine.tick(0, 0, true, width);
        check(engine.activeSlots()[engine.activeIndices().front()].y == 0,
              "Seek restarts lane allocation from the top");
        engine.load({{0, Mode::Scroll, "scroll", 0}, {0, Mode::Top, "top", 0},
                     {0, Mode::Bottom, "bottom", 0}});
        engine.tick(0, 0, true, width);
        check(engine.activeCount() == 3 && engine.activeSlots()[engine.activeIndices()[1]].y == 40 &&
                  engine.activeSlots()[engine.activeIndices()[1]].x == 350 &&
                  engine.activeSlots()[engine.activeIndices()[2]].y == 160 &&
                  engine.activeSlots()[engine.activeIndices()[2]].x == 350,
              "Fixed comments remain centered and anchored to their own edge");
        engine.load({{0, Mode::Top, "one", 0}, {0, Mode::Top, "two", 0},
                     {0, Mode::Top, "three", 0}, {0, Mode::Top, "four", 0},
                     {0, Mode::Top, "overflow", 0}});
        engine.tick(0, 0, true, width);
        check(engine.activeCount() == (overlap ? 5u : 4u) && engine.dropped() == (overlap ? 0u : 1u),
              "Overlap setting controls full-lane fallback without dropping when enabled");
        if (overlap) {
            engine.seek(0);
            engine.tick(0, 0, true, width);
            check(engine.activeSlots()[engine.activeIndices().back()].y == 0,
                  "Seek also resets overlap fallback allocation");
        }
    }
    o.overlap = false;
    o.maxTracks = 4;
    engine.configure(o, 800, 160);
    engine.load({{0, Mode::Top, "large", 0}, {0, Mode::Scroll, "normal", 0}});
    const auto sized = [](const Item& item) {
        return Engine::Extent(100, item.text == "large" ? 60 : 40);
    };
    engine.tick(0, 0, true, sized);
    check(engine.activeCount() == 2 && engine.activeSlots()[engine.activeIndices()[0]].height == 60 &&
              engine.activeSlots()[engine.activeIndices()[1]].y == 80,
          "Large source fonts reserve every intersected physical lane");
    engine.load({{0, Mode::Bottom, "large", 0}, {0, Mode::Bottom, "normal", 0}});
    engine.tick(0, 0, true, sized);
    check(engine.activeCount() == 2 && engine.activeSlots()[engine.activeIndices()[0]].y == 100 &&
              engine.activeSlots()[engine.activeIndices()[1]].y == 40,
          "Large bottom fonts stay inside the viewport and block intersecting lanes");
    for (const bool overlap : {false, true}) {
        o.overlap = overlap;
        engine.configure(o, 800, 30);
        engine.load({{0, Mode::Top, "large", 0}});
        engine.tick(0, 0, true, sized);
        check(engine.activeCount() == 0 && engine.dropped() == 1,
              "A viewport shorter than one lane never emits out-of-bounds comments");
        engine.configure(o, 800, 160);
        engine.load({{0, Mode::Top, "large", 0}});
        engine.tick(0, 0, true, [](const Item&) { return Engine::Extent(100, 200); });
        check(engine.activeCount() == 0, "An oversized font cannot escape the configured track region");
    }
    engine.configure(o, 800, 160);
    engine.load({{0, Mode::Scroll, std::string(512, 'x'), 0xffffff}});
    engine.tick(0, 0, true, width);
    const auto oldId = engine.activeSlots()[engine.activeIndices().front()].id;
    engine.clear();
    check(engine.items().size() == 1 && engine.activeSlots().size() > 0,
          "Clearing the visible timeline preserves the loaded file and pool");
    engine.unload();
    check(engine.items().empty() && engine.items().capacity() == 0 &&
              engine.activeSlots().capacity() == 0 && engine.activeIndices().capacity() == 0 &&
              engine.activeCount() == 0 && engine.position() == 0 && engine.finished() && engine.dropped() == 0,
          "Unload releases file and pool storage and resets the timeline");
    engine.unload();
    engine.reconfigure(o, 800, 160);
    check(engine.activeSlots().capacity() == 0, "Settings after unload do not recreate the pool");
    engine.tick(0, 0, true, width);
    engine.load({{0, Mode::Scroll, "reloaded", 0xffffff}});
    engine.tick(0, 0, true, width);
    check(engine.activeCount() == 1 && engine.activeSlots()[engine.activeIndices().front()].id > oldId &&
              engine.dropped() == 0,
          "Reload recreates pool and occupancy with the retained options");
    for (const double laneHeight : {39.4, 45.2, 40.0}) for (const bool overlap : {false, true}) {
        o = {}; o.trackHeight = laneHeight; o.maxTracks = 60; o.maxActive = 600; o.overlap = overlap;
        engine.configure(o, 2560, 1440);
        std::vector<Item> dense(600);
        for (auto& item : dense) item.text = "scroll";
        engine.load(std::move(dense));
        engine.tick(0, 0, true, [&](const Item&) { return Engine::Extent(100, laneHeight); });
        const int lanes = static_cast<int>(std::ceil(1440 / laneHeight));
        int last = 0;
        for (auto slot : engine.activeIndices()) {
            const auto& a = engine.activeSlots()[slot];
            check(a.y < 1440 && a.y + a.height <= lanes * laneHeight + 1e-8,
                  "Partial scroll lane starts on screen and exceeds only the rounded track region");
            if (std::abs(a.y - (lanes - 1) * laneHeight) < 1e-8) ++last;
        }
        check(last > 0, "Fractional and exact lane heights include the final scrolling lane");
        check(engine.activeCount() == (overlap ? 600 : static_cast<std::size_t>(lanes)),
              "Overlap and safe admission agree on the number of scrolling lanes");
    }
    o = {}; o.trackHeight = 40; o.maxTracks = 60;
    engine.configure(o, 800, 30);
    engine.load({{0, Mode::Scroll, "partial"}});
    engine.tick(0, 0, true, width);
    check(engine.activeCount() == 1 && engine.activeSlots()[engine.activeIndices()[0]].y == 0,
          "A short viewport permits a partially visible scrolling lane");
    engine.configure(o, 800, 85);
    engine.load({{0, Mode::Scroll, "a"}, {0, Mode::Scroll, "b"}, {0, Mode::Scroll, "partial"},
                 {0, Mode::Bottom, "blocked"}});
    engine.tick(0, 0, true, width);
    check(engine.activeCount() == 3 && engine.dropped() == 1,
          "The visible portion of the partial scroll lane blocks bottom fixed comments");
    const auto partialId = engine.activeSlots()[engine.activeIndices().back()].id;
    engine.reconfigure(o, 800, 81);
    check(engine.activeCount() == 3 && engine.activeSlots()[engine.activeIndices().back()].id == partialId &&
              engine.activeSlots()[engine.activeIndices().back()].y == 80,
          "Reflow preserves the ID and position of a still partially visible scroll lane");
    engine.tick(0, 10, false, width);
    check(engine.activeCount() == 3, "Pause preserves partial lane occupancy");
    engine.reconfigure(o, 800, 80);
    check(engine.activeCount() == 2 && engine.retiredBySettings() == 1,
          "Reflow retires a final lane when it becomes entirely outside the viewport");
    o.maxTracks = 2;
    engine.configure(o, 800, 85);
    engine.load({{0, Mode::Scroll, "a"}, {0, Mode::Scroll, "b"}, {0, Mode::Scroll, "limited"}});
    engine.tick(0, 0, true, width);
    check(engine.activeCount() == 2, "Partial screen admission still respects the user's track cap");
    engine.configure(o, 800, 85);
    engine.load({{0, Mode::Scroll, "oversized"}});
    engine.tick(0, 0, true, [](const Item&) { return Engine::Extent(100, 1e100); });
    check(engine.activeCount() == 0 && engine.dropped() == 1,
          "Extremely large finite text heights are rejected before converting lane counts");
    return failures ? 1 : 0;
}
