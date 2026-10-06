#include "core/DanmakuEngine.h"
#include <iostream>
#include <limits>
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
    return failures ? 1 : 0;
}
