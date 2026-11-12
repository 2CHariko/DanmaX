#include "core/PlaybackClock.h"
#include "core/MediaClock.h"
#include <cmath>
#include <chrono>
#include <iostream>

using namespace std::chrono_literals;
int main() {
    danmaku::PlaybackClock clock;
    auto check = [](bool condition, const char* message) {
        if (!condition)
            std::cerr << message << '\n';
        return condition;
    };
    bool ok = true;
    clock.advance(20ms);
    ok &= check(clock.position() == 0us, "Initially paused clock must not advance");
    clock.setPaused(false);
    clock.advance(9ms);
    clock.advance(27ms);
    ok &= check(clock.position() == 36ms, "Variable frame intervals must accumulate exactly");
    clock.setPaused(true);
    clock.advance(10s);
    ok &= check(clock.position() == 36ms, "Pause must exclude wall-clock time");
    clock.setPaused(false);
    clock.advance(-1s);
    clock.advance(14ms);
    ok &= check(clock.position() == 50ms, "Resume must not jump or accept negative deltas");
    clock.reset();
    ok &= check(clock.position() == 0us && !clock.paused(), "Reset preserves playback state");
    danmaku::MediaClock media;
    media.synchronize(10, 1, true, 0, 0, true);
    media.advance(0.2);
    media.synchronize(10.2, 2, true, 0.2, 0.2);
    media.advance(0.3);
    ok &= check(std::abs(media.position() - 10.4) < 1e-9 &&
                std::abs(media.takeMotion() - 0.4) < 1e-9, "Rate change divides motion at sample boundary");
    media.synchronize(10.4, 2, false, 0.3, 0.3);
    media.advance(100);
    ok &= check(std::abs(media.position() - 10.4) < 1e-9 && media.takeMotion() == 0,
                "Paused media excludes wall time");
    media.synchronize(10.4, 0.5, true, 100, 100);
    media.advance(102);
    ok &= check(std::abs(media.takeMotion() - 1) < 1e-9, "Resume and half speed");
    ok &= check(media.synchronize(3, 1, true, 102, 102), "Backward seek reported");
    ok &= check(media.takeMotion() == 0 && media.position() == 3, "Seek resets motion");
    media.freeze(103);
    media.takeMotion();
    media.advance(200);
    ok &= check(media.position() == 4 && media.takeMotion() == 0, "Timeout freeze");
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-9; };
    for (double delay : {0.30, 1.00}) {
        danmaku::MediaClock delayed;
        delayed.synchronize(10, 1, true, 100, 100);
        delayed.advance(100.5 + delay);
        const auto previousMotion = delayed.takeMotion();
        ok &= check(!delayed.synchronize(10.5, 1, true, 100.5, 100.5 + delay) &&
                    near(delayed.position(), 10.5 + delay) && near(previousMotion, 0.5 + delay) &&
                    near(delayed.takeMotion(), 0),
                    "Late continuous sample preserves position and does not repeat animation motion");
        ok &= check(delayed.synchronize(0, 1, true, 101, 101 + delay) && delayed.position() == 0,
                    "Real backward seek in a delayed sample resets at the raw position");
        delayed.advance(101 + delay);
        ok &= check(near(delayed.takeMotion(), delay), "Motion after seek starts at its observation");
        ok &= check(delayed.synchronize(60, 1, true, 102, 102 + delay), "Delayed forward seek still resets");
    }
    danmaku::MediaClock changes;
    changes.synchronize(10, 1, true, 100, 100);
    ok &= check(!changes.synchronize(10.5, 2, true, 100.5, 101.0) && near(changes.position(), 11.5) &&
                near(changes.takeMotion(), 1.5), "Late rate change splits unconsumed motion at observation");
    changes.advance(101.5);
    ok &= check(!changes.synchronize(11.5, 2, false, 101, 101.5) && near(changes.position(), 11.5) &&
                near(changes.takeMotion(), 0), "Late pause discards pending motion after the observed pause");
    changes.advance(102);
    ok &= check(!changes.synchronize(11.5, 1, true, 101.5, 102) && near(changes.position(), 12.0) &&
                near(changes.takeMotion(), 0.5), "Late resume starts motion at observation");
    return ok ? 0 : 1;
}
