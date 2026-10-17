#include "application/SeekController.h"
#include <algorithm>
#include <cmath>
#include <utility>

SeekController::SeekController(MediaMonitor* monitor, QObject* parent, int waitMs, int lateMs)
    : QObject(parent), monitor_(monitor), waitMs_(waitMs), lateMs_(lateMs) {
    connect(this, &SeekController::dispatchRequested, monitor_, &MediaMonitor::requestSeek);
    connect(monitor_, &MediaMonitor::seekDispatched, this, &SeekController::dispatched);
    // Deterministic injected transports also use the raw acknowledgement port.
    // Detailed production receipts arrive first and suppress this fallback.
    connect(monitor_, &MediaMonitor::seekFinished, this, [this](quint64 id, bool accepted, const QString& error) {
        if (!current_ || current_->id != id || current_->dispatched) return;
        const auto now = MediaMonitor::monotonicSeconds();
        dispatched({id, accepted ? SeekDispatch::Outcome::Submitted : SeekDispatch::Outcome::Rejected,
                    current_->expected, now, now, error});
    });
    connect(monitor_, &MediaMonitor::sampleReceived, this, &SeekController::observe, Qt::QueuedConnection);
    timer_.setInterval(50); connect(&timer_, &QTimer::timeout, this, &SeekController::tick);
}
void SeekController::requestSeek(MediaSample expected, double seconds, quint64 id) {
    if (!std::isfinite(seconds) || seconds < 0 || !std::isfinite(expected.duration) ||
        expected.duration <= 0 || seconds > expected.duration || !expected.canSeek) {
        emit finished(id, false, QStringLiteral("定位目标无效或播放器不支持定位")); return;
    }
    Request request; request.id = id; request.expected = std::move(expected); request.target = seconds;
    expired_.reset();
    if (current_) {
        if (!request.expected.sameConnection(current_->expected) || request.expected.contentDiffers(current_->expected)) {
            emit finished(id, false, QStringLiteral("当前播放任务已改变，未提交定位")); return;
        }
        const auto replaced = queued_ ? queued_->id : 0;
        queued_ = std::move(request);
        if (replaced) emit finished(replaced, false, {});
        return;
    }
    start(std::move(request));
}
void SeekController::start(Request request) {
    const auto now = MediaMonitor::monotonicSeconds();
    const auto age = now - request.expected.observedAt;
    if (!request.expected.usableTimeline() || !std::isfinite(age) || age < 0 || age > 2.5) {
        emit finished(request.id, false, QStringLiteral("尚无新鲜播放位置，未提交定位")); return;
    }
    request.started = now; request.expires = now + waitMs_ / 1000.0;
    current_ = std::move(request); timer_.start();
    emit dispatchRequested(current_->expected, current_->target, current_->id);
}
void SeekController::dispatched(const SeekDispatch& result) {
    if (!current_ || current_->id != result.request || current_->dispatched) return;
    if (result.before.usableTimeline() && (!result.before.sameConnection(current_->expected) ||
        result.before.contentDiffers(current_->expected))) { cancelAll(); return; }
    if (result.outcome == SeekDispatch::Outcome::Rejected) {
        const auto id = current_->id; const auto queued = queued_ ? queued_->id : 0;
        current_.reset(); queued_.reset();
        const auto error = result.error.isEmpty() ? QStringLiteral("播放器拒绝定位") : result.error;
        emit finished(id, false, error); if (queued) emit finished(queued, false, error);
        return;
    }
    current_->dispatch = result; current_->dispatched = true;
    if (result.before.usableTimeline()) current_->expected = result.before;
}
bool SeekController::confirm(Request& request, const MediaSample& sample, bool& approximate) {
    if (!request.dispatched || !sample.usableTimeline()) return false;
    const auto& receipt = request.dispatch;
    if (!std::isfinite(receipt.issuedAt) || !std::isfinite(receipt.returnedAt) ||
        receipt.issuedAt < request.started || receipt.returnedAt < receipt.issuedAt) return false;
    if (sample.observedAt <= receipt.returnedAt || sample.observedAt <= receipt.issuedAt) return false;
    const double rate = sample.playing && std::isfinite(sample.rate) && sample.rate > 0 ? sample.rate : 0;
    const double low = request.target; // Applying an asynchronous command may occur after its acknowledgement.
    const double high = request.target + std::max(0.0, sample.observedAt - receipt.issuedAt) * rate;
    const double oldRate = request.expected.playing ? request.expected.rate : 0;
    const double continued = request.expected.position + (sample.observedAt - request.expected.observedAt) * oldRate;
    const bool moved = std::abs(sample.position - continued) > 0.35 &&
        std::abs(sample.position - request.expected.position) > 0.35;
    const bool alreadyThere = receipt.outcome == SeekDispatch::Outcome::Submitted &&
        std::abs(request.target - request.expected.position) <= 1;
    if (sample.position >= low - 1 && sample.position <= high + 1 && (moved || alreadyThere)) {
        approximate = false; return true;
    }
    // An unspecified protocol may land on an earlier frame. Report the observed
    // landing explicitly rather than widening the "exact target reached" test.
    if (request.expected.seekPrecision == MediaSample::SeekPrecision::Precise ||
        sample.seekPrecision == MediaSample::SeekPrecision::Precise || !moved) {
        request.stable = 0; return false;
    }
    if (request.stable) {
        const double elapsed = sample.observedAt - request.firstLanding.observedAt;
        const double predicted = request.firstLanding.position + elapsed * rate;
        if (sample.playback == request.firstLanding.playback && std::abs(sample.position - predicted) <= 0.35 && elapsed >= 0.15) {
            approximate = true; return true;
        }
        if (std::abs(sample.position - predicted) > 0.35) request.stable = 0;
    }
    if (!request.stable) { request.firstLanding = sample; request.stable = 1; }
    return false;
}
void SeekController::observe(const MediaSample& sample) {
    auto* request = current_ ? &*current_ : expired_ ? &*expired_ : nullptr;
    if (!request || sample.id != request->expected.id) return;
    if (sample.generation != request->expected.generation || sample.observedAt <= request->lastObserved) return;
    if (!sample.sameConnection(request->expected) || sample.contentDiffers(request->expected) ||
        sample.connection == MediaSample::Connection::Exited) { cancelAll(); return; }
    const double age = MediaMonitor::monotonicSeconds() - sample.observedAt;
    if (!std::isfinite(age) || age < 0 || age > 2.5) return;
    request->lastObserved = sample.observedAt;
    bool approximate{};
    if (confirm(*request, sample, approximate)) complete(sample, approximate);
}
void SeekController::complete(const MediaSample& sample, bool approximate) {
    const auto id = current_ ? current_->id : expired_->id;
    current_.reset(); expired_.reset();
    auto next = std::exchange(queued_, {});
    emit landed(id, sample, approximate); emit finished(id, true, {});
    // Use the latest confirmed timeline, never the stale sample captured while dragging.
    if (next) { next->expected = sample; start(std::move(*next)); }
    else timer_.stop();
}
void SeekController::tick() {
    const auto now = MediaMonitor::monotonicSeconds();
    if (current_ && now >= current_->expires) {
        auto old = std::move(*current_); current_.reset();
        const auto queued = queued_ ? queued_->id : 0; queued_.reset();
        old.expires = now + lateMs_ / 1000.0; expired_ = std::move(old);
        const auto error = QStringLiteral("播放器尚未确认定位，未自动重发，请等待响应后重试");
        emit finished(expired_->id, false, error); if (queued) emit finished(queued, false, error);
    }
    if (expired_ && now >= expired_->expires) expired_.reset();
    if (!current_ && !expired_) timer_.stop();
}
void SeekController::cancelAll() {
    const auto id = current_ ? current_->id : 0, queued = queued_ ? queued_->id : 0;
    current_.reset(); queued_.reset(); expired_.reset(); timer_.stop();
    if (id) emit finished(id, false, {}); if (queued) emit finished(queued, false, {});
}
