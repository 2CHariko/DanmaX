#include "core/DanmakuEngine.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
using namespace danmaku;
int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << message << '\n'; ++failures; }
    };
    const auto near = [](double a, double b) { return std::abs(a - b) < 1e-8; };
    const auto extent = [](const Item&) { return Engine::Extent(100, 40); };
    const auto snapshots = [](const Engine& engine) {
        std::vector<Active> result;
        for (auto slot : engine.activeIndices()) result.push_back(engine.activeSlots()[slot]);
        return result;
    };
    Options o;
    o.speed = 100; o.trackHeight = 40; o.maxTracks = 6; o.maxActive = 8;
    Engine engine;
    engine.configure(o, 800, 400);
    engine.load({{0, Mode::Scroll, "scroll"}, {0, Mode::Top, "top"}, {0, Mode::Bottom, "bottom"},
                 {1, Mode::Scroll, "future"}});
    engine.tick(0, 0, true, extent);
    engine.tick(0.2, 0.2, true, extent);
    auto before = snapshots(engine);
    o.speed = 200;
    engine.reconfigure(o, 800, 400);
    auto after = snapshots(engine);
    check(after.size() == 3 && after[0].id == before[0].id && after[0].x == before[0].x &&
              after[1].remaining == before[1].remaining && engine.position() == 0.2,
          "Speed edit preserves IDs, coordinates, fixed lifetime and timeline");
    engine.tick(0.3, 0.1, true, extent);
    check(near(snapshots(engine)[0].x, before[0].x - 20), "Next frame uses the updated common speed");
    for (int i = 0; i < 30; ++i) {
        o.speed = 100 + i;
        engine.reconfigure(o, 800, 400);
    }
    engine.tick(0.3, 0, true, extent);
    check(engine.activeCount() == 3 && snapshots(engine)[0].id == before[0].id && engine.dropped() == 0,
          "Repeated edits do not rewind the cursor, replay or drop consumed records");
    o.fixedSeconds = 8;
    engine.reconfigure(o, 800, 400);
    check(near(snapshots(engine)[1].remaining, 7.7), "Duration extension subtracts already displayed animation time");
    engine.tick(0.3, 100, false, extent);
    o.fixedSeconds = 4;
    engine.reconfigure(o, 800, 400);
    check(near(snapshots(engine)[1].remaining, 3.7), "Paused duration edit excludes paused wall time");
    o.fixedSeconds = 0.5;
    engine.reconfigure(o, 800, 400);
    engine.tick(0.6, 0.3, true, extent);
    check(engine.activeCount() == 1, "Shortened fixed lifetime expires normally without clearing scrolls");
    o.fixedSeconds = 10;
    engine.reconfigure(o, 800, 400);
    engine.tick(1, 0.4, true, extent);
    check(engine.activeCount() == 2 && snapshots(engine)[0].id == before[0].id,
          "Extending duration does not resurrect expired comments; future cursor still works");

    o.maxActive = 5; o.overlap = true; o.fixedSeconds = 0.5;
    engine.configure(o, 800, 400);
    engine.load({{0, Mode::Top, "expire a"}, {0, Mode::Bottom, "expire b"},
                 {0, Mode::Scroll, "high slot a"}, {0, Mode::Scroll, "high slot b"},
                 {0, Mode::Scroll, "high slot c"}, {0.8, Mode::Scroll, "new"}});
    engine.tick(0, 0, true, extent);
    engine.tick(0.6, 0.6, true, extent);
    before = snapshots(engine);
    o.maxActive = 3;
    engine.reconfigure(o, 800, 400);
    after = snapshots(engine);
    check(after.size() == 3 && after[0].id == before[0].id && after[2].id == before[2].id &&
              engine.retiredBySettings() == 0,
          "Pool shrink preserves high-numbered live slots when their count fits");
    o.maxActive = 2;
    engine.reconfigure(o, 800, 400);
    check(engine.activeCount() == 2 && engine.retiredBySettings() == 1 && engine.dropped() == 0 &&
              snapshots(engine)[0].id == before[0].id,
          "Capacity reduction retires only excess newest comments and keeps separate statistics");
    o.maxActive = 5;
    engine.reconfigure(o, 800, 400);
    engine.tick(0.8, 0.2, true, extent);
    check(engine.activeCount() == 3 && snapshots(engine)[0].id == before[0].id,
          "Expanded pool admits future records without restarting live comments");
    engine.tick(20, 19.2, true, extent);
    engine.seek(0);
    engine.tick(0, 0, true, extent);
    check(engine.activeCount() == 5, "Pool/free-list remains valid after shrink, expand, retirement and seek");

    o = {}; o.speed = 100; o.trackHeight = 40; o.maxTracks = 6;
    engine.configure(o, 800, 400);
    engine.load({{0, Mode::Scroll, "scroll"}, {0, Mode::Top, "top"}, {0, Mode::Bottom, "bottom"}});
    engine.tick(0, 0, true, extent);
    engine.tick(0.2, 0.2, true, extent);
    before = snapshots(engine);
    o.trackHeight = 60;
    int measures = 0;
    engine.reconfigure(o, 800, 400, [&](const Item&) { ++measures; return Engine::Extent(200, 60); });
    after = snapshots(engine);
    check(after.size() == 3 && measures == 3 && after[0].id == before[0].id && after[0].x == before[0].x &&
              after[0].width == 200 && after[1].x == 300 && after[2].y == 340 &&
              near(after[1].remaining, before[1].remaining),
          "Live font remeasurement preserves scrolling progress, centers fixed text and anchors bottom text");
    engine.reconfigure(o, 1000, 500);
    after = snapshots(engine);
    check(after[0].x == before[0].x && after[1].x == 400 && after[2].y == 440,
          "Viewport resizing preserves scroll coordinates and updates fixed anchors");
    o.maxTracks = 1;
    engine.reconfigure(o, 1000, 500);
    check(engine.activeCount() == 2 && engine.retiredBySettings() == 1,
          "Reduced track region keeps fitting top/bottom comments and retires only conflicts");

    o = {}; o.speed = 100; o.trackHeight = 40; o.maxTracks = 2;
    engine.configure(o, 800, 80);
    engine.load({{0, Mode::Scroll, "a"}, {1.2, Mode::Scroll, "b"}});
    engine.tick(0, 0, true, extent);
    engine.tick(1.2, 1.2, true, extent);
    before = snapshots(engine);
    check(before.size() == 2 && before[1].y == 0, "Fixture has two spaced scrolls on the same lane");
    engine.reconfigure(o, 800, 80, [](const Item&) { return Engine::Extent(200, 40); });
    after = snapshots(engine);
    check(after.size() == 2 && after[0].x == before[0].x && after[1].x == before[1].x && after[1].y == 40,
          "Font growth resolves actual live rectangle collisions by moving only conflicting text");
    engine.tick(1.3, 0.1, true, extent);
    check(engine.activeCount() == 2, "Remeasured comments remain live after the next tick");

    o.overlap = true;
    engine.configure(o, 800, 80);
    engine.load({{0, Mode::Scroll, "a"}, {0, Mode::Scroll, "b"}, {0, Mode::Scroll, "c"}});
    engine.tick(0, 0, true, extent);
    before = snapshots(engine);
    o.overlap = false;
    engine.reconfigure(o, 800, 80);
    after = snapshots(engine);
    check(after.size() == 2 && after[0].id == before[0].id && after[0].y != after[1].y &&
              engine.retiredBySettings() == 1, "Disabling overlap reflows existing collisions with minimal retirement");
    engine.reconfigure(o, 800, 80, [](const Item&) { return Engine::Extent(100, 200); });
    check(engine.activeCount() == 0 && engine.retiredBySettings() == 3,
          "Text larger than the viewport retires without resetting the consumed cursor");
    engine.reconfigure(o, 800, 80, extent);
    engine.tick(0, 0, true, extent);
    check(engine.activeCount() == 0, "Settings retirement cannot replay already consumed records");
    engine.seek(0);
    engine.tick(0, 0, true, extent);
    engine.reconfigure(o, 800, 80, [](const Item&) {
        return Engine::Extent(std::numeric_limits<double>::quiet_NaN(), 40);
    });
    check(engine.activeCount() == 0, "Invalid remeasurement cannot corrupt occupancy");

    o = {}; o.trackHeight = 40; o.maxTracks = 6; o.speed = 100;
    engine.configure(o, 800, 400);
    engine.load({{0, Mode::Top, "fixed"}, {0, Mode::Scroll, "scroll"}});
    engine.tick(0, 0, true, extent);
    engine.tick(2, 2, true, extent);
    const auto scrollingId = snapshots(engine).back().id;
    o.fixedSeconds = 1;
    engine.reconfigure(o, 800, 400);
    check(engine.activeCount() == 1 && snapshots(engine)[0].id == scrollingId,
          "Duration shortened below displayed age immediately retires only the expired fixed comment");

    // Exercise interacting geometry/capacity changes and free-slot reuse, including paused edits.
    std::vector<Item> stress;
    for (int i = 0; i < 200; ++i)
        stress.push_back({i * 0.1, i % 5 == 0 ? Mode::Bottom : i % 5 == 1 ? Mode::Top : Mode::Scroll, "stress"});
    o = {}; o.trackHeight = 40; o.maxTracks = 8; o.maxActive = 16;
    engine.configure(o, 800, 400);
    engine.load(stress);
    std::vector<std::uint64_t> seen(stress.size());
    double position = 0;
    for (int step = 0; step < 200; ++step) {
        o.maxActive = 2 + step % 15;
        o.maxTracks = 2 + step % 7;
        o.trackHeight = 20 + (step % 3) * 20;
        o.fixedSeconds = 0.5 + (step % 8);
        o.overlap = step % 3 == 0;
        o.speed = 30 + step % 200;
        const auto textSize = [&](const Item&) { return Engine::Extent(80 + step % 4 * 40, o.trackHeight); };
        engine.reconfigure(o, 600 + step % 3 * 100, 300 + step % 2 * 100, textSize);
        const bool playing = step % 4 != 0;
        if (playing) position += 0.1;
        engine.tick(position, 0.1, playing, textSize);
        const auto live = snapshots(engine);
        const auto alive = std::count_if(engine.activeSlots().begin(), engine.activeSlots().end(),
                                        [](const Active& a) { return a.alive; });
        check(live.size() == engine.activeCount() && live.size() <= static_cast<std::size_t>(o.maxActive) &&
                  static_cast<std::size_t>(alive) == live.size(), "Stress preserves capacity and slot/index invariants");
        for (const auto& a : live) {
            const double screenHeight = 300 + step % 2 * 100;
            const double allowedBottom = a.mode == Mode::Scroll ?
                std::min(o.maxTracks * o.trackHeight, std::ceil(screenHeight / o.trackHeight) * o.trackHeight)
                : screenHeight;
            check(a.y >= 0 && a.y < screenHeight && a.y + a.height <= allowedBottom + 1e-8 && a.remaining > 0,
                  "Stress permits only the partial final scroll lane outside the viewport");
            check(seen[a.item] == 0 || seen[a.item] == a.id, "Stress never replays an already presented source record");
            seen[a.item] = a.id;
        }
        if (!o.overlap) for (std::size_t i = 0; i < live.size(); ++i) for (std::size_t j = i + 1; j < live.size(); ++j) {
            const auto& a = live[i]; const auto& b = live[j];
            const bool vertical = a.y + a.height > b.y && b.y + b.height > a.y;
            const bool safe = a.mode == Mode::Scroll && b.mode == Mode::Scroll &&
                              (a.x + a.width + 16 <= b.x || b.x + b.width + 16 <= a.x);
            check(!vertical || safe, "Stress maintains cross-mode avoidance and scrolling gaps after live edits");
        }
    }
    return failures ? 1 : 0;
}
