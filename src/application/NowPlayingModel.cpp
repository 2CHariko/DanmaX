#include "application/NowPlayingModel.h"
#include "application/AppController.h"
#include <QFileInfo>
#include <QUrl>
#include <atomic>
#include <cmath>
namespace { quint64 nextCommand() { static std::atomic<quint64> id{quint64(1) << 59}; return ++id; } }
NowPlayingModel::NowPlayingModel(AppController* app) : QObject(app), app_(app) {
    connect(this, &NowPlayingModel::playbackRequested, &app_->monitor_, &MediaMonitor::requestPlayback);
    connect(this, &NowPlayingModel::seekRequested, &app_->monitor_, &MediaMonitor::requestSeek);
    connect(&app_->danmaku_, &DanmakuSession::changed, this, &NowPlayingModel::refresh);
    connect(&app_->danmaku_, &DanmakuSession::sourceFailed, this, [this](const QString& error) { error_ = error; emit changed(); });
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { finish(false, QStringLiteral("未能确认播放器操作，请检查播放器后重试")); });
    connect(app_, &AppController::stateChanged, this, &NowPlayingModel::refresh);
    connect(app_, &AppController::metricsChanged, this, &NowPlayingModel::refresh);
    connect(&app_->coordinator_, &PlaybackCoordinator::changed, this, &NowPlayingModel::refresh);
    connect(&app_->media_, &MediaSession::stateChanged, this, &NowPlayingModel::refresh);
    connect(&app_->media_, &MediaSession::sourceCommitted, this, [this] { error_.clear(); emit changed(); });
    connect(&app_->danmaku_, &DanmakuSession::sourceCommitted, this, [this] { error_.clear(); emit changed(); });
    connect(&app_->videoOpen_, &VideoOpenService::failed, this, [this] { error_.clear(); emit changed(); });
    connect(&app_->monitor_, &MediaMonitor::playbackControlFinished, this, [this](quint64 id, bool success, const QString& error) {
        if (!pending_ || id != request_ || desiredPosition_ >= 0) return;
        if (!success) finish(false, error); else { accepted_ = true; if (observed_) finish(true, {}); }
    });
    connect(&app_->monitor_, &MediaMonitor::seekFinished, this, [this](quint64 id, bool success, const QString& error) {
        if (!pending_ || id != request_ || desiredPosition_ < 0) return;
        if (!success) finish(false, error); else { accepted_ = true; if (observed_) finish(true, {}); }
    });

}
bool NowPlayingModel::video() const { return app_->coordinator_.owner() == PlaybackCoordinator::Media; }
bool NowPlayingModel::standalone() const { return app_->danmaku_.active(); }
bool NowPlayingModel::openingStandalone() const { return app_->danmaku_.preparing(); }
bool NowPlayingModel::canSelectSource() const { return standalone() || app_->media_.canSelectSource(); }
QObject* NowPlayingModel::sourceSession() const { return standalone() ? static_cast<QObject*>(&app_->danmaku_) : static_cast<QObject*>(&app_->media_); }
quint64 NowPlayingModel::contextId() const { return app_->coordinator_.generation(); }
bool NowPlayingModel::ownsPlayer() const { return video() && app_->media_.boundSession().startsWith("danmax-potplayer:"); }
bool NowPlayingModel::external() const { return video(); }
bool NowPlayingModel::active() const { return standalone() || app_->media_.active(); }
bool NowPlayingModel::playing() const { return standalone() ? app_->danmaku_.playing() : fresh() && sample_.playing; }
bool NowPlayingModel::busy() const { return standalone() ? app_->danmaku_.preparing() : pending_ || app_->videoOpen_.pending() || app_->media_.restoring_ || app_->media_.loading_ || (!video() && openingStandalone()); }
bool NowPlayingModel::fresh() const {
    const double age = MediaMonitor::monotonicSeconds() - sample_.observedAt;
    return video() && active() && app_->media_.confirmed_ && app_->media_.sameContent(sample_)
        && sample_.found && sample_.generation == app_->mediaGeneration_ && sample_.id == app_->boundSession()
        && std::isfinite(age) && age >= 0 && age <= 2.5 && app_->targetSessionConnected_
        && (sample_.playback == MediaSample::Playback::Playing || sample_.playback == MediaSample::Playback::Paused);
}
bool NowPlayingModel::canPlay() const { return standalone() ? !busy() && count() > 0 : !busy() && fresh() && (playing() ? sample_.canPause : sample_.canPlay); }
bool NowPlayingModel::canSeek() const { return standalone() ? !busy() && count() > 0 && duration() > 0 : !busy() && fresh() && sample_.canSeek && duration() > 0; }
QVariantMap NowPlayingModel::metadata() const { return standalone() ? app_->danmaku_.metadata_ : video() ? app_->media_.contentMetadata_ : QVariantMap{}; }
QString NowPlayingModel::title() const {
    if (standalone()) {
        const auto title = app_->danmaku_.metadata_.value("animeTitle").toString();
        return !title.isEmpty() ? title : QFileInfo(app_->danmaku_.sourceTitle()).fileName().isEmpty()
            ? QStringLiteral("独立弹幕") : QFileInfo(app_->danmaku_.sourceTitle()).fileName();
    }
    if (!video()) return QStringLiteral("准备播放");
    const auto value = metadata().value("animeTitle").toString();
    return value.isEmpty() ? app_->media_.title() : value;
}
QString NowPlayingModel::subtitle() const {
    const auto episode = metadata().value("episodeTitle").toString();
    if (standalone()) return QStringLiteral("独立弹幕") + (episode.isEmpty() ? QString() : QStringLiteral(" · ") + episode);
    return video() ? QStringLiteral("视频") + (episode.isEmpty() ? QString() : QStringLiteral(" · ") + episode) : QString();
}
QString NowPlayingModel::coverUrl() const { return metadata().value("imageUrl").toString(); }
QString NowPlayingModel::source() const {
    if (standalone()) {
        const auto server = QUrl(metadata().value("server").toString()).host();
        return server.isEmpty() ? QFileInfo(app_->danmaku_.sourceTitle()).fileName() : server;
    }
    if (!video()) return {};
    const auto server = QUrl(app_->media_.source_.value("metadata").toMap().value("server").toString()).host();
    const auto title = app_->media_.sourceTitle_;
    return !server.isEmpty() ? server : QFileInfo(title).isAbsolute() ? QFileInfo(title).fileName() : title;
}
QString NowPlayingModel::status() const {
    if (standalone()) return app_->danmaku_.preparing() ? QStringLiteral("正在准备新的弹幕来源…")
        : app_->danmaku_.completed() ? QStringLiteral("独立弹幕播放完成")
        : count() == 0 ? app_->danmaku_.status() : playing() ? QStringLiteral("正在独立播放弹幕") : QStringLiteral("独立弹幕已暂停");
    if (!video() && openingStandalone()) return QStringLiteral("正在读取独立弹幕 XML…");
    if (pending_) return desiredPosition_ >= 0 ? QStringLiteral("正在确认定位…") : QStringLiteral("正在确认播放状态…");
    if (!video()) return app_->media_.error().isEmpty() ? QStringLiteral("打开视频，或从更多菜单独立播放弹幕") : app_->media_.status();
    if (fresh() && !busy()) return (playing() ? QStringLiteral("正在播放 · ") : QStringLiteral("已暂停 · ")) + app_->media_.status_;
    return app_->media_.status_;
}
QString NowPlayingModel::error() const { return !error_.isEmpty() ? error_ : standalone() ? app_->danmaku_.error_ : app_->media_.error_; }
QString NowPlayingModel::controlHint() const {
    if (standalone()) return QStringLiteral("播放／暂停控制独立弹幕时间轴");
    if (!active()) return QStringLiteral("请打开视频或独立播放弹幕");
    if (busy()) return QStringLiteral("正在处理播放任务");
    if (!fresh()) return QStringLiteral("等待本次播放器连接");
    return QStringLiteral("播放／暂停控制当前视频；弹幕开关只改变显示");
}
int NowPlayingModel::count() const { return standalone() ? app_->danmaku_.total() : video() ? app_->media_.total_ : 0; }
double NowPlayingModel::position() const { return standalone() ? app_->danmaku_.position() : video() ? app_->media_.position() : 0; }
double NowPlayingModel::duration() const { return standalone() ? app_->danmaku_.duration() : video() ? app_->media_.duration() : 0; }
bool NowPlayingModel::same(const MediaSample& sample) const {
    return sample.found && sample.id == expected_.id && sample.generation == expected_.generation
        && sample.sessionToken == expected_.sessionToken && sample.title == expected_.title && sample.mediaUri == expected_.mediaUri;
}

void NowPlayingModel::refresh() {
    if (displayGeneration_ != app_->coordinator_.generation()) {
        displayGeneration_ = app_->coordinator_.generation(); error_.clear();
    }
    if (pending_ && (ownerGeneration_ != app_->coordinator_.generation() || expected_.generation != app_->mediaGeneration_)) finish(false, {});
    emit changed();
}
void NowPlayingModel::observe(const MediaSample& sample) {
    if (sample.generation != app_->mediaGeneration_ || sample.id != app_->boundSession()) return;
    sample_ = sample;
    if (pending_) {
        if (!same(sample)) finish(false, QStringLiteral("播放器内容或连接已改变，已取消操作"));
        else if (sample.observedAt > requestedAt_) {
            const double advance = sample.playing ? (sample.observedAt - requestedAt_) * sample.rate : 0;
            observed_ = desiredPosition_ >= 0 ? std::abs(sample.position - desiredPosition_ - advance) <= 1 : sample.playing == desiredPlay_;
            if (observed_ && accepted_) finish(true, {});
        }
    }
    emit changed();
}
void NowPlayingModel::request(bool play, double seconds) {
    expected_ = sample_; desiredPlay_ = play; desiredPosition_ = seconds; requestedAt_ = MediaMonitor::monotonicSeconds();
    ownerGeneration_ = app_->coordinator_.generation(); request_ = nextCommand(); pending_ = true; accepted_ = observed_ = false; error_.clear();
    deadline_.start(seconds >= 0 ? 5000 : 3000); emit changed();
    if (seconds >= 0) emit seekRequested(expected_, seconds, request_);
    else emit playbackRequested(expected_, play, request_);
}
void NowPlayingModel::finish(bool success, const QString& error) {
    deadline_.stop(); pending_ = false; error_ = success ? QString() : error; emit changed();
}

void NowPlayingModel::togglePlayback() {
    if (!canPlay()) return;
    if (standalone()) { if (!app_->running_) app_->start(true); else app_->togglePause(); }
    else request(!playing());
}
void NowPlayingModel::seek(double seconds) {
    if (!canSeek() || !std::isfinite(seconds) || seconds < 0 || seconds > duration()) return;
    if (standalone()) app_->seek(seconds); else request(false, seconds);
}
void NowPlayingModel::endPlayback() {
    finish(true, {});
    app_->handoff(PlaybackCoordinator::None); app_->videoOpen_.cancel(); app_->configure();
    app_->mediaGeneration_ = app_->monitor_.select(app_->boundSession(), true); sample_ = {};
    app_->targetSessionConnected_ = false; app_->connectionObservedAt_ = 0;
    emit app_->stateChanged(); emit changed();
}

void NowPlayingModel::openStandalone(const QString& path) { error_.clear(); app_->danmaku_.openFile(path); }
void NowPlayingModel::cancelStandaloneOpen() { app_->danmaku_.cancelPreparation(); emit changed(); }
void NowPlayingModel::replaceDanmaku(const QString& path) {
    error_.clear();
    if (standalone()) app_->danmaku_.loadFile(path); else { app_->danmaku_.cancelPreparation(); app_->media_.loadFile(path); }
}
void NowPlayingModel::unloadDanmaku() {
    error_.clear();
    if (standalone()) app_->danmaku_.unload(); else { app_->danmaku_.cancelPreparation(); app_->media_.unload(); }
}
void NowPlayingModel::searchDanmaku() {
    if (standalone()) app_->danmaku_.searchDanmaku(); else { app_->danmaku_.cancelPreparation(); app_->media_.searchDanmaku(); }
}
