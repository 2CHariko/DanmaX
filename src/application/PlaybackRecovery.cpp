#include "application/PlaybackRecovery.h"
PlaybackRecovery::PlaybackRecovery(QObject* parent) : QObject(parent) {
    deadline_.setSingleShot(true); deadline_.setInterval(3000);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        pending_ = awaitingPause_ = owned_ = false; ++request_;
        status_ = QStringLiteral("未能确认播放器暂停，请手动控制播放"); emit changed();
    });
}
bool PlaybackRecovery::same(const MediaSample& sample) const {
    const double age = MediaMonitor::monotonicSeconds() - sample.observedAt;
    return sample.found && age >= 0 && age <= 2.5 && sample.id == origin_.id &&
        sample.sessionToken == origin_.sessionToken && sample.generation == origin_.generation && sample.title == origin_.title &&
        (sample.playback == MediaSample::Playback::Playing || sample.playback == MediaSample::Playback::Paused);
}
void PlaybackRecovery::begin(const MediaSample& sample, const QString& reason) {
    if (active_ && same(sample)) { reason_ = reason; emit changed(); return; }
    invalidate(); origin_ = latest_ = sample; reason_ = reason; active_ = true;
    if (sample.playback == MediaSample::Playback::Playing) {
        pending_ = true; playingCommand_ = false; status_ = QStringLiteral("正在请求指定播放器暂停…");
        deadline_.start(); emit controlRequested(sample, false, ++request_);
    } else status_ = QStringLiteral("播放器原本已暂停，完成或取消后保持暂停");
    emit changed(); emit showRequested();
}
void PlaybackRecovery::observe(const MediaSample& sample) {
    if (!active_ && !pending_ && !awaitingPause_ && !owned_) return;
    if (!same(sample)) { invalidate(); return; }
    latest_ = sample;
    if (awaitingPause_ && sample.playback == MediaSample::Playback::Paused) {
        awaitingPause_ = false; owned_ = true; deadline_.stop();
        status_ = QStringLiteral("指定播放器已暂停"); emit changed(); resumeIfOwned();
    } else if (owned_ && sample.playback == MediaSample::Playback::Playing) {
        owned_ = false; // User resumed while choosing; never issue another pause.
        status_ = QStringLiteral("播放器已继续播放"); emit changed();
    }
}
void PlaybackRecovery::controlFinished(quint64 request, bool success, const QString& error) {
    if (request != request_ || !pending_) return;
    pending_ = false;
    if (playingCommand_) {
        deadline_.stop(); owned_ = false;
        status_ = success ? QStringLiteral("已请求继续播放") : QStringLiteral("未能恢复播放：") + error;
    } else if (success) {
        awaitingPause_ = true;
        observe(latest_); // A Paused sample may have arrived before the command acknowledgement.
    } else {
        deadline_.stop();
        status_ = QStringLiteral("未能自动暂停，请手动暂停：") + error;
    }
    emit changed();
}
void PlaybackRecovery::finish(bool resume) {
    active_ = false; resumeWanted_ = resume;
    emit changed(); resumeIfOwned();
}
void PlaybackRecovery::resumeIfOwned() {
    if (!resumeWanted_ || !owned_ || pending_ || !same(latest_) || latest_.playback != MediaSample::Playback::Paused) return;
    resumeWanted_ = false; owned_ = false; pending_ = true; playingCommand_ = true;
    deadline_.start(); emit controlRequested(latest_, true, ++request_);
}
void PlaybackRecovery::invalidate() {
    deadline_.stop(); ++request_; active_ = pending_ = awaitingPause_ = owned_ = resumeWanted_ = playingCommand_ = false;
    origin_ = latest_ = {}; reason_.clear(); status_.clear(); emit changed();
}
