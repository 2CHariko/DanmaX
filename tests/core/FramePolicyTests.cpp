#include "core/FramePolicy.h"
#include "core/DanmakuEngine.h"
#include <iostream>
#include <vector>
using namespace danmaku;
int main() {
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        if (!ok) { std::cerr << message << '\n'; ++failures; }
    };
    FramePacer pacer;
    pacer.reset(0, 60);
    pacer.started(0);
    check(pacer.wait(6000000) == 10666667, "Rendering work is subtracted from the absolute deadline");
    check(pacer.requestWait(6000000, 6000000) == 4666667 && !pacer.due(10666667),
          "Prefetching a platform request never advances the render deadline");
    check(pacer.requestWait(6000000, 20000000) == 0 && !pacer.due(6000000),
          "A large delivery prediction requests early without permitting early animation");
    check(pacer.requestWait(6000000, -1) == pacer.wait(6000000), "Negative delivery prediction is ignored");
    pacer.started(19000000);
    check(pacer.wait(20000000) == 13333334, "Vsync lateness does not drift the next deadline");
    pacer.started(1000000000);
    check(pacer.wait(1000000000) > 0 && pacer.wait(1000000000) <= 16666667,
          "Long stall skips missed deadlines without a burst");
    for (const int hz : {60, 144, 165, 240}) {
        pacer.reset(0, 60);
        int frames = 0;
        for (int vsync = 0; vsync < hz * 10; ++vsync) {
            const auto now = static_cast<std::int64_t>(std::llround(vsync * 1e9 / hz));
            if (pacer.due(now)) { ++frames; pacer.started(now); }
        }
        check(frames >= 599 && frames <= 601, "Non-divisible refresh rates preserve the ten-second request average");
    }
    pacer.reset(0, 0);
    pacer.started(0);
    check(pacer.due(1) && pacer.wait(1) == 0, "Follow-display mode adds no software interval");

    AdaptiveFramePolicy policy;
    double time = 2;
    policy.configure(100, 60, true, 0);
    policy.observe(0, true, 100);
    policy.observe(1.9, true, 100, true);
    check(policy.limit() == 100 && policy.state() == AdaptiveFramePolicy::State::Warming, "Warmup never reduces admission");
    policy.observe(time, true, 100);
    const auto second = [&](int hz, std::size_t active) {
        for (int i = 0; i < hz; ++i) policy.observe(time + i / static_cast<double>(hz), true, active, true);
        time += 1;
        policy.observe(time, true, active);
    };
    second(30, 100);
    check(policy.limit() == 100, "One slow window does not reduce admission");
    second(60, 100);
    second(30, 100);
    check(policy.limit() == 100, "A healthy window breaks the slow streak");
    second(30, 100);
    check(policy.limit() == 90 && policy.state() == AdaptiveFramePolicy::State::Draining, "Two slow windows reduce by ten percent without evicting live comments");
    for (int i = 0; i < 8; ++i) second(20, 100);
    check(policy.limit() == 90, "Draining cannot cascade to the floor while old comments are still live");
    for (int i = 0; i < 4; ++i) second(60, 20);
    check(policy.limit() == 90, "Recovery requires five complete healthy windows");
    second(60, 20);
    check(policy.limit() == 95, "Recovery adds five percent of user capacity");
    policy.observe(time, false, 20);
    check(policy.limit() == 95 && policy.state() == AdaptiveFramePolicy::State::Inactive, "Pause preserves the runtime quota and clears feedback");
    time += 10;
    policy.observe(time, true, 20);
    check(policy.state() == AdaptiveFramePolicy::State::Warming && policy.limit() == 95, "Resume starts fresh warmup without counting hidden time");
    policy.configure(101, 60, true, 0);
    time = 2;
    policy.observe(0, true, 1);
    policy.observe(time, true, 1);
    for (int i = 0; i < 100; ++i) second(1, 1);
    check(policy.limit() == 26 && policy.floor() == 26 && policy.state() == AdaptiveFramePolicy::State::Unmet,
          "Ceiling quarter-capacity floor reports an unmet target");
    policy.configure(101, 60, false, time);
    check(policy.limit() == 101 && policy.state() == AdaptiveFramePolicy::State::Disabled, "Disabling restores full admission");
    policy.configure(100, 60, true, 0);
    time = 2; policy.observe(0, true, 10); policy.observe(time, true, 10);
    for (int i = 0; i < 8; ++i) second(90, 10);
    check(policy.limit() == 100, "Ninety FPS already satisfies a sixty FPS target");

    Engine engine;
    Options options;
    options.maxActive = 6; options.overlap = true; options.fixedSeconds = .5;
    engine.configure(options, 800, 400);
    std::vector<Item> items;
    for (int i = 0; i < 6; ++i) items.push_back({0, Mode::Top, "live"});
    items.push_back({.2, Mode::Bottom, "skip"});
    items.push_back({.7, Mode::Scroll, "future"});
    engine.load(items);
    int measurements = 0;
    const auto measure = [&](const Item&) { ++measurements; return Engine::Extent(100, 36); };
    engine.tick(0, 0, true, measure);
    const auto id = engine.activeSlots()[engine.activeIndices()[0]].id;
    const auto pool = engine.activeSlots().size();
    engine.setAdmissionLimit(2);
    engine.tick(.2, .2, true, measure);
    check(engine.activeCount() == 6 && engine.activeSlots().size() == pool &&
          engine.activeSlots()[engine.activeIndices()[0]].id == id && measurements == 6 &&
          engine.adaptiveSkipped() == 1 && engine.dropped() == 0 && engine.retiredBySettings() == 0,
          "Admission skips before measuring; live IDs, storage and other drop reasons are preserved");
    engine.tick(.2, 5, false, measure);
    check(engine.activeCount() == 6, "Paused admission change cannot age live comments");
    engine.tick(.6, .4, true, measure);
    check(engine.activeCount() == 0, "Existing comments retire naturally");
    engine.setAdmissionLimit(6);
    engine.tick(.7, .1, true, measure);
    check(engine.activeCount() == 1 && engine.adaptiveSkipped() == 1 && measurements == 7,
          "Restoring admission accepts future comments without replaying skipped records");
    engine.unload();
    check(engine.adaptiveSkipped() == 0, "Unload resets adaptive skip statistics");
    engine.configure(options, 800, 400);
    engine.load({{0, Mode::Scroll, "scroll"}, {0, Mode::Top, "top"}, {0, Mode::Bottom, "bottom"}, {0, Mode::Scroll, "skip"}});
    engine.setAdmissionLimit(3);
    engine.tick(0, 0, true, measure);
    check(engine.activeCount() == 3 && engine.adaptiveSkipped() == 1 &&
          engine.activeSlots()[engine.activeIndices()[2]].mode == Mode::Bottom,
          "Mixed types share one arrival-order admission quota");
    return failures ? 1 : 0;
}
