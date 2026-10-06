#include "core/PlaybackClock.h"
#include <chrono>
#include <iostream>

using namespace std::chrono_literals;
int main() {
    danmaku::PlaybackClock clock;
    auto check = [](bool condition, const char* message) {
        if (!condition) std::cerr << message << '\n';
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
    return ok ? 0 : 1;
}
