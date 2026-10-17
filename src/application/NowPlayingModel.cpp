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
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] { finish(false, QStringLiteral("未能确认播放器操作，请检查播放器后重试")); });
    connect(app_, &AppController::stateChanged, this, &NowPlayingModel::refresh);
    connect(app_, &AppController::metricsChanged, this, &NowPlayingModel::refresh);
    connect(&app_->coordinator_, &PlaybackCoordinator::changed, this, &NowPlayingModel::refresh);
    connect(&app_->media_, &MediaSession::stateChanged, this, &NowPlayingModel::refresh);
    connect(&app_->videoOpen_, &VideoOpenService::failed, this, [this] { error_.clear(); emit changed(); });
    connect(&app_->monitor_, &MediaMonitor::playbackControlFinished, this, [this](quint64 id, bool success, const QString& error) {
        if (!pending_ || id != request_ || desiredPosition_ >= 0) return;
        if (!success) finish(false, error); else { accepted_ = true; if (observed_) finish(true, {}); }
    });
    connect(&app_->monitor_, &MediaMonitor::seekFinished, this, [this](quint64 id, bool success, const QString& error) {
        if (!pending_ || id != request_ || desiredPosition_ < 0) return;
        if (!success) finish(false, error); else { accepted_ = true; if (observed_) finish(true, {}); }
    });
    connect(app_, &AppController::loadCompleted, this, [this](bool success) {
        if (pendingLoad_ && pendingLoad_ == app_->loadGeneration_) {
            pendingLoad_ = 0;
            if (success) {
                app_->start(pendingManual_);
                if (pendingManual_ && pendingPaused_ && app_->playing_) app_->togglePause();
                emit app_->requestNavigateToPage(1);
            }
            else error_ = app_->danmaku_.error();
            emit changed();
        } else if (pendingEpisode_ && ownerGeneration_ == app_->coordinator_.generation() && !video()) {
            pendingEpisode_ = false; if (success) app_->start(app_->danmaku_.manual_);
        }
    });
}
bool NowPlayingModel::video() const { return app_->coordinator_.owner() == PlaybackCoordinator::Media; }
bool NowPlayingModel::external() const { return video() || (app_->coordinator_.owner() == PlaybackCoordinator::Danmaku && !app_->manual_); }
bool NowPlayingModel::fresh() const {
    const double age = MediaMonitor::monotonicSeconds() - sample_.observedAt;
    return external() && (!video() || (app_->media_.active_ && app_->media_.confirmed_ && app_->media_.sameContent(sample_)))
        && sample_.found && sample_.generation == app_->mediaGeneration_ && sample_.id == app_->boundSession()
        && std::isfinite(age) && age >= 0 && age <= 2.5 && app_->targetSessionConnected_
        && (sample_.playback == MediaSample::Playback::Playing || sample_.playback == MediaSample::Playback::Paused);
}
bool NowPlayingModel::active() const { return video() ? app_->media_.active_ : app_->running_; }
bool NowPlayingModel::playing() const { return external() ? fresh() && sample_.playing : active() && app_->playing_; }
bool NowPlayingModel::busy() const { return pending_ || pendingLoad_ || (video() ? app_->videoOpen_.pending() || app_->media_.restoring_ : app_->loading_); }
bool NowPlayingModel::canPlay() const {
    if (busy()) return false;
    if (!video() && app_->coordinator_.owner() == PlaybackCoordinator::Danmaku && app_->danmaku_.total() == 0
        && app_->automatic_.enabled()) return false;
    if (external() && (video() || app_->running_)) return fresh() && (playing() ? sample_.canPause : sample_.canPlay);
    return app_->danmaku_.total() > 0 && (app_->danmaku_.manual_ || !app_->settings_.values()["targetSession"].toString().isEmpty())
        || (!app_->danmaku_.manual_ && app_->settings_.values()["automaticDanmaku"].toBool()
            && !app_->settings_.values()["targetSession"].toString().isEmpty());
}
bool NowPlayingModel::canSeek() const {
    if (busy() || duration() <= 0) return false;
    return external() ? fresh() && sample_.canSeek && sample_.id.startsWith("danmax-vlc:") : active() && app_->manual_;
}
QVariantMap NowPlayingModel::metadata() const { return video() ? app_->media_.contentMetadata_ : app_->danmaku_.metadata_; }
bool NowPlayingModel::canSelectEpisode() const { return !video() && !metadata().value("animeTitle").toString().isEmpty(); }
QString NowPlayingModel::title() const {
    const auto meta = metadata();
    if (!meta.value("animeTitle").toString().isEmpty()) return meta.value("animeTitle").toString();
    const auto source = video() ? app_->media_.title() : app_->danmaku_.title_;
    return source.isEmpty() ? QStringLiteral("准备播放") : QFileInfo(source).isAbsolute() ? QFileInfo(source).fileName() : source;
}
QString NowPlayingModel::subtitle() const {
    const auto episode = metadata().value("episodeTitle").toString();
    return (video() ? QStringLiteral("视频") : !app_->danmaku_.manual_ ? QStringLiteral("跟随播放器的弹幕") : QStringLiteral("独立弹幕"))
        + (episode.isEmpty() ? QString() : QStringLiteral(" · ") + episode);
}
QString NowPlayingModel::coverUrl() const { return metadata().value("imageUrl").toString(); }
QString NowPlayingModel::source() const {
    const auto meta = video() ? app_->media_.source_.value("metadata").toMap() : app_->danmaku_.metadata_;
    const auto server = QUrl(meta.value("server").toString()).host();
    const auto title = video() ? app_->media_.sourceTitle_ : app_->danmaku_.title_;
    return !server.isEmpty() ? server : QFileInfo(title).isAbsolute() ? QFileInfo(title).fileName() : title;
}
QString NowPlayingModel::status() const {
    if (pending_) return desiredPosition_ >= 0 ? QStringLiteral("正在确认定位…") : QStringLiteral("正在确认播放状态…");
    if (!video() && app_->coordinator_.owner() == PlaybackCoordinator::Danmaku && app_->danmaku_.total() == 0 && app_->automatic_.enabled())
        return app_->automatic_.status();
    if (video() && fresh() && !busy()) return (playing() ? QStringLiteral("正在播放 · ") : QStringLiteral("已暂停 · ")) + app_->media_.status_;
    return video() ? app_->media_.status_ : app_->danmaku_.status();
}
QString NowPlayingModel::error() const { return !error_.isEmpty() ? error_ : video() ? app_->media_.error_ : app_->danmaku_.error_; }
QString NowPlayingModel::controlHint() const {
    if (busy()) return QStringLiteral("正在处理播放任务");
    if (external() && !fresh()) return QStringLiteral("等待播放器连接");
    if (external() && !canPlay()) return QStringLiteral("当前播放器未提供此控制能力，请在播放器中操作");
    return external() ? QStringLiteral("播放／暂停控制外部播放器；弹幕开关只改变显示") : QStringLiteral("播放／暂停控制弹幕时间轴");
}
int NowPlayingModel::count() const { return video() ? app_->media_.total_ : app_->danmaku_.total(); }
double NowPlayingModel::position() const { return video() ? app_->media_.position() : app_->danmaku_.position(); }
double NowPlayingModel::duration() const { return video() ? app_->media_.duration() : app_->danmaku_.duration(); }
bool NowPlayingModel::same(const MediaSample& sample) const {
    return sample.found && sample.id == expected_.id && sample.generation == expected_.generation
        && sample.sessionToken == expected_.sessionToken && sample.title == expected_.title && sample.mediaUri == expected_.mediaUri;
}
void NowPlayingModel::refresh() {
    if (displayGeneration_ != app_->coordinator_.generation()) {
        displayGeneration_ = app_->coordinator_.generation(); error_.clear(); pendingEpisode_ = false;
    }
    if (pending_ && (ownerGeneration_ != app_->coordinator_.generation() || expected_.generation != app_->mediaGeneration_)) finish(false, {});
    if (pendingLoad_ && pendingLoad_ != app_->loadGeneration_) pendingLoad_ = 0;
    emit changed();
}
void NowPlayingModel::observe(const MediaSample& sample) {
    if (sample.generation != app_->mediaGeneration_ || sample.id != app_->boundSession()) return;
    sample_ = sample;
    if (pending_ && startPending_) {
        if (!same(sample)) finish(false, QStringLiteral("播放器内容已改变，请重新播放"));
        else if (sample.playing) finish(true, {});
        else if (sample.canPlay) request(true);
        else finish(false, QStringLiteral("播放器不支持继续播放，请在播放器中操作"));
    }
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
    startPending_ = false;
    expected_ = sample_; desiredPlay_ = play; desiredPosition_ = seconds; requestedAt_ = MediaMonitor::monotonicSeconds();
    ownerGeneration_ = app_->coordinator_.generation(); request_ = nextCommand(); pending_ = true; accepted_ = observed_ = false; error_.clear();
    deadline_.start(seconds >= 0 ? 5000 : 3000); emit changed();
    if (seconds >= 0) emit seekRequested(expected_, seconds, request_);
    else emit playbackRequested(expected_, play, request_);
}
void NowPlayingModel::finish(bool success, const QString& error) {
    deadline_.stop(); pending_ = startPending_ = false; error_ = success ? QString() : error; emit changed();
}
void NowPlayingModel::togglePlayback() {
    if (!canPlay()) return;
    error_.clear();
    if (!active() && !video()) {
        const auto previous = sample_;
        const double age = MediaMonitor::monotonicSeconds() - previous.observedAt;
        const bool resume = !app_->danmaku_.manual_ && previous.found && previous.id == app_->boundSession()
            && previous.generation == app_->mediaGeneration_ && std::isfinite(age) && age >= 0 && age <= 2.5
            && previous.playback == MediaSample::Playback::Paused && previous.canPlay;
        app_->start(app_->danmaku_.manual_);
        if (resume && app_->running_) {
            expected_ = previous; expected_.generation = app_->mediaGeneration_;
            ownerGeneration_ = app_->coordinator_.generation(); request_ = nextCommand();
            pending_ = startPending_ = true; accepted_ = observed_ = false; desiredPosition_ = -1; desiredPlay_ = true;
            deadline_.start(3000); emit changed();
        }
        return;
    }
    if (external()) request(!playing()); else app_->togglePause();
}
void NowPlayingModel::seek(double seconds) {
    if (!canSeek() || !std::isfinite(seconds) || seconds < 0 || seconds > duration()) return;
    if (external()) request(false, seconds); else app_->seek(seconds);
}
void NowPlayingModel::endPlayback() {
    pendingLoad_ = 0; pendingEpisode_ = false; finish(true, {});
    app_->handoff(PlaybackCoordinator::None); app_->videoOpen_.cancel(); app_->configure();
    app_->mediaGeneration_ = app_->monitor_.select(app_->boundSession(), true); sample_ = {}; emit changed();
}
void NowPlayingModel::openDanmaku(const QString& path) {
    pendingEpisode_ = false; pendingManual_ = true; pendingPaused_ = false;
    error_.clear(); app_->loadFile(path); pendingLoad_ = app_->loadGeneration_; emit changed();
}
void NowPlayingModel::replaceDanmaku(const QString& path) {
    pendingLoad_ = 0; pendingEpisode_ = false; error_.clear();
    if (video()) app_->media_.loadFile(path);
    else {
        const bool restart = app_->running_; pendingManual_ = app_->manual_; pendingPaused_ = !app_->playing_;
        app_->loadFile(path); if (restart) pendingLoad_ = app_->loadGeneration_;
    }
    emit changed();
}
void NowPlayingModel::unloadDanmaku() { pendingLoad_ = 0; pendingEpisode_ = false; if (video()) app_->media_.unload(); else app_->unloadDanmaku(); }
void NowPlayingModel::searchDanmaku() { pendingLoad_ = 0; error_.clear(); if (video()) app_->media_.searchDanmaku(); else app_->showOnlineDanmakuForAnime({}); }
void NowPlayingModel::selectEpisode() {
    if (!canSelectEpisode()) return;
    pendingEpisode_ = active(); ownerGeneration_ = app_->coordinator_.generation();
    const auto meta = metadata(); app_->showOnlineDanmakuForAnime(meta.value("animeTitle").toString(), {}, 0);
}
