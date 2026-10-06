#include "renderer/PreviewDriver.h"

PreviewDriver::PreviewDriver(QObject* parent) : QObject(parent) {
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &PreviewDriver::tick);
}
double PreviewDriver::elapsedSeconds() const {
    return std::chrono::duration<double>(clock_.position()).count();
}
bool PreviewDriver::running() const { return !clock_.paused(); }
void PreviewDriver::setRunning(bool running) {
    if (running == this->running()) return;
    if (running) {
        elapsed_.start();
        clock_.setPaused(false);
        timer_.start();
    } else {
        tick();
        timer_.stop();
        clock_.setPaused(true);
    }
    emit runningChanged();
}
void PreviewDriver::reset() {
    clock_.reset();
    if (running()) elapsed_.restart();
    emit frameChanged();
}
void PreviewDriver::tick() {
    if (!running() || !elapsed_.isValid()) return;
    const auto delta = std::chrono::nanoseconds(elapsed_.nsecsElapsed());
    elapsed_.restart();
    clock_.advance(std::chrono::duration_cast<danmaku::PlaybackClock::Duration>(delta));
    emit frameChanged();
}
