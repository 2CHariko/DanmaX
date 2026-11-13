#include "application/PlaybackRecovery.h"
#include <atomic>
namespace { quint64 nextControlRequest() { static std::atomic<quint64> sequence{0}; return ++sequence; } }
PlaybackRecovery::PlaybackRecovery(QObject* parent) : QObject(parent) {
    deadline_.setSingleShot(true); deadline_.setInterval(3000);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        pending_ = acknowledged_ = owned_ = resumeWanted_ = false; request_ = nextControlRequest();
        warning_ = true;
        status_ = playingCommand_ ? QStringLiteral("未能确认播放器已继续播放，请检查播放器")
                                  : QStringLiteral("未能确认播放器暂停，请手动控制播放");
        emit changed();
    });
}
bool PlaybackRecovery::same(const MediaSample& sample) const {
    const double age = MediaMonitor::monotonicSeconds() - sample.observedAt;
    return sample.usableTimeline() && age >= 0 && age <= 2.5 && sample.sameConnection(origin_) && !sample.contentDiffers(origin_);
}
void PlaybackRecovery::prepare(const MediaSample& sample) {
    if (same(sample) && (preparing_ || owned_ || (pending_ && !playingCommand_))) {
        preparing_ = true; resumeWanted_ = false;
        return;
    }
    invalidate(); origin_ = latest_ = sample; preparing_ = true;
    if (!same(sample)) {
        warning_ = true; status_ = QStringLiteral("播放器状态不可用，无法自动暂停");
    } else if (sample.playback == MediaSample::Playback::Paused) {
        status_ = QStringLiteral("播放器原本已暂停，完成或取消后保持暂停");
    } else if (!sample.canPause) {
        warning_ = true; status_ = QStringLiteral("播放器不支持自动暂停，请手动控制播放");
    } else {
        pending_ = true; requestedAt_ = MediaMonitor::monotonicSeconds();
        status_ = QStringLiteral("正在请求指定播放器暂停…");
        deadline_.start(); emit controlRequested(sample, false, (request_ = nextControlRequest()));
    }
    emit changed();
}
void PlaybackRecovery::begin(const MediaSample& sample, const QString& reason) {
    if (!(preparing_ && sample.sameConnection(origin_) && !sample.contentDiffers(origin_))) prepare(sample);
    const bool wasActive = active_;
    reason_ = reason; active_ = true;
    emit changed();
    if (!wasActive) emit showRequested();
}
void PlaybackRecovery::dismissPrompt() {
    if (active_) { active_ = false; emit changed(); }
}
void PlaybackRecovery::observe(const MediaSample& sample) {
    if (!preparing_ && !pending_ && !owned_) return;
    if (!sample.sameConnection(origin_) || sample.contentDiffers(origin_) || sample.connection == MediaSample::Connection::Exited ||
        sample.playback == MediaSample::Playback::Stopped || sample.playback == MediaSample::Playback::Closed) { invalidate(); return; }
    if (!same(sample)) { latest_ = sample; return; }
    latest_ = sample;
    if (owned_ && sample.playback == MediaSample::Playback::Playing) {
        owned_ = resumeWanted_ = false; // The user resumed; do not pause again.
        status_ = QStringLiteral("播放器已继续播放"); emit changed();
    }
    confirmControl();
    resumeIfOwned();
}
void PlaybackRecovery::controlFinished(quint64 request, bool success, const QString& error) {
    if (request != request_ || !pending_) return;
    if (success) {
        acknowledged_ = true;
        confirmControl(); // The readback can arrive before the transport reply.
        resumeIfOwned();
        return;
    }
    deadline_.stop(); pending_ = acknowledged_ = resumeWanted_ = false;
    warning_ = true;
    status_ = (playingCommand_ ? QStringLiteral("未能恢复播放：") : QStringLiteral("未能自动暂停，请手动控制播放：")) + error;
    emit changed();
}
void PlaybackRecovery::confirmControl() {
    if (!pending_ || !acknowledged_ || !same(latest_) || latest_.observedAt <= requestedAt_ ||
        latest_.playback != (playingCommand_ ? MediaSample::Playback::Playing : MediaSample::Playback::Paused)) return;
    deadline_.stop(); pending_ = acknowledged_ = false;
    owned_ = !playingCommand_ && !manualOverride_;
    status_ = playingCommand_ ? QStringLiteral("播放器已继续播放") : QStringLiteral("指定播放器已暂停");
    emit changed();
}
void PlaybackRecovery::manualPlaybackRequested() {
    if (!preparing_ && !owned_ && !pending_) return;
    manualOverride_ = true; owned_ = resumeWanted_ = false;
}
void PlaybackRecovery::finish(bool resume) {
    if (!resume) { invalidate(); return; }
    active_ = preparing_ = false;
    resumeWanted_ = !manualOverride_ && (owned_ || (pending_ && !playingCommand_));
    emit changed(); resumeIfOwned();
}
void PlaybackRecovery::resumeIfOwned() {
    if (!resumeWanted_ || !owned_ || pending_ || !same(latest_) || latest_.playback != MediaSample::Playback::Paused) return;
    resumeWanted_ = owned_ = false;
    if (!latest_.canPlay) {
        warning_ = true; status_ = QStringLiteral("播放器不支持自动恢复，请手动继续播放"); emit changed(); return;
    }
    pending_ = playingCommand_ = true; requestedAt_ = MediaMonitor::monotonicSeconds();
    status_ = QStringLiteral("正在确认播放器继续播放…"); emit changed();
    deadline_.start(); emit controlRequested(latest_, true, (request_ = nextControlRequest()));
}
void PlaybackRecovery::invalidate() {
    deadline_.stop(); request_ = nextControlRequest();
    active_ = preparing_ = pending_ = acknowledged_ = owned_ = resumeWanted_ = playingCommand_ = warning_ = manualOverride_ = false;
    origin_ = latest_ = {}; reason_.clear(); status_.clear(); emit changed();
}
