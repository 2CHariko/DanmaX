#include "application/MediaSession.h"
#include "application/AppController.h"
#include "infrastructure/ExternalPlayerStore.h"
#include "infrastructure/XmlLoader.h"
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScopedValueRollback>
#include <atomic>
#include <cmath>
namespace {
quint64 nextRequest() { static std::atomic<quint64> next{quint64(1) << 62}; return ++next; }
QString associationDirectory(const QString& root) {
    const auto directory = QDir(root).filePath("media"); QDir().mkpath(directory); return directory;
}
}
MediaSession::MediaSession(const QString& data, const QString& cache, AppController* app, VideoOpenService* service,
                           ExternalPlayerStore* players, MediaMonitor* monitor)
    : QObject(app), app_(app), service_(service), players_(players), monitor_(monitor), recent_(data, this),
      automatic_(associationDirectory(data), cache, this), library_(cache, this) {
    cacheRoot_ = cache;
    seekDeadline_.setSingleShot(true); seekDeadline_.setInterval(5000);
    connect(&seekDeadline_, &QTimer::timeout, this, [this] { restoreError(QStringLiteral("未能确认恢复位置，请重试或按当前位置继续")); });
    saveTimer_.setInterval(5000); connect(&saveTimer_, &QTimer::timeout, this, &MediaSession::flush);
    connect(service_, &VideoOpenService::validated, this, &MediaSession::beginTask);
    connect(this, &MediaSession::seekRequested, monitor_, &MediaMonitor::requestSeek);
    connect(service_, &VideoOpenService::opening, this, [this](const QString& id) {
        starting_ = false;
        bound_ = id; app_->bindMedia(id); status_ = QStringLiteral("正在确认播放器中的目标视频…"); emit stateChanged();
    });
    connect(service_, &VideoOpenService::confirmed, this, &MediaSession::confirmed);
    connect(service_, &VideoOpenService::failed, this, [this](const QString& error) { error_ = error; status_ = QStringLiteral("打开失败，可重新打开视频"); emit stateChanged(); });
    connect(service_, &VideoOpenService::cancelled, this, [this] {
        if (active_ && !confirmed_ && !starting_) { deactivate(); status_ = QStringLiteral("已取消打开视频"); emit stateChanged(); }
    });
    connect(service_, &VideoOpenService::changed, this, &MediaSession::stateChanged);
    connect(monitor_, &MediaMonitor::seekFinished, this, [this](quint64 request, bool success, const QString& error) {
        if (!active_ || !restoring_ || request != seekRequest_ || seekTask_ != task_) return;
        if (!success) { restoreError(error); return; }
        seekAccepted_ = true;
        if (seekObserved_) prepared();
    });
    connect(automatic_.playbackRecovery(), &PlaybackRecovery::controlRequested, monitor_, &MediaMonitor::requestPlayback);
    connect(monitor_, &MediaMonitor::playbackControlFinished, automatic_.playbackRecovery(), &PlaybackRecovery::controlFinished);
    connect(automatic_.playbackRecovery(), &PlaybackRecovery::showRequested, app_, &AppController::showMainWindow);
    connect(&automatic_, &AutomaticDanmaku::changed, this, &MediaSession::stateChanged);
    connect(&automatic_, &AutomaticDanmaku::sourceReady, this, [this] {
        if (!active_ || !ready_ || manualSource_) return;
        const auto result = automatic_.takeReadyResult(); if (!result) return;
        if (contentMetadata_.isEmpty()) contentMetadata_ = result->metadata;
        accept(std::move(result->items), result->metadata.value("animeTitle").toString() + QStringLiteral(" · ")
            + result->metadata.value("episodeTitle").toString(), {{"kind", "online"}, {"metadata", result->metadata}});
    });
    connect(&library_, &DanmakuLibrary::sourceReady, this, [this] {
        const auto result = library_.takeReadyResult(); if (!result || video_.isEmpty()) return;
        const auto title = result->metadata.value("animeTitle").toString() + QStringLiteral(" · ")
            + result->metadata.value("episodeTitle").toString();
        const QVariantMap source{{"kind", "online"}, {"metadata", result->metadata}};
        if (!active_) {
            source_ = source; sourceTitle_ = title; total_ = static_cast<int>(result->items.size());
            recent_.checkpoint(video_, latest_.position, latest_.duration, source_);
            status_ = QStringLiteral("已为下次播放选择缓存弹幕"); emit stateChanged(); return;
        }
        if (!ready_) return;
        automatic_.suspend(false); manualSource_ = true;
        accept(std::move(result->items), title, source);
    });
    connect(&library_, &DanmakuLibrary::operationStarted, this, [this] {
        if (active_) { automatic_.suspend(false); manualSource_ = true; }
    });
    connect(&library_, &DanmakuLibrary::changed, this, [this] {
        if (active_ && ready_ && !library_.busy() && !library_.error().isEmpty()) {
            error_ = library_.error(); automatic_.playbackRecovery()->begin(latest_, error_); emit stateChanged();
        }
    });
}
MediaSession::~MediaSession() { flush(); cancelLoader(); automatic_.reset(); library_.cancel(); }
QString MediaSession::title() const { return QFileInfo(video_).fileName(); }
bool MediaSession::connected() const { return active_ && app_->targetSessionConnected(); }
void MediaSession::configure(const QStringList& servers) {
    if (servers_ == servers) return;
    servers_ = servers; library_.setServers(servers);
    automatic_.configure(active_ && ready_ && !manualSource_, servers);
}
void MediaSession::openVideo(const QString& file, int intent) {
    if (intent < Normal || intent > FromStart) return;
    intent_ = intent; opening_ = true;
    service_->configureProfile(players_->current()); service_->open({QUrl::fromLocalFile(RecentVideoStore::normalizedPath(file))});
    opening_ = false;
}
void MediaSession::beginTask(const QString& path) {
    app_->handoff(PlaybackCoordinator::Media);
    ++task_; active_ = starting_ = true; confirmed_ = ready_ = writing_ = manualSource_ = false;
    restoring_ = restoreFailed_ = false; latest_ = {}; bound_.clear(); video_ = path;
    total_ = 0; sourceTitle_.clear(); error_.clear(); source_ = recent_.lookup(path).value("source").toMap();
    const auto record = recent_.lookup(path);
    contentMetadata_ = record.value("content").toMap();
    if (contentMetadata_.isEmpty() && source_.value("kind") == "online") contentMetadata_ = source_.value("metadata").toMap();
    restorePosition_ = record.value("position").toDouble();
    if (restorePosition_ < 5 || restorePosition_ >= record.value("duration").toDouble() - 10) restorePosition_ = 0;
    if (intent_ == FromStart) restorePosition_ = 0;
    status_ = QStringLiteral("正在打开视频…"); saveTimer_.start();
    emit stateChanged(); emit app_->requestNavigateToPage(1, -1);
}
void MediaSession::confirmed() {
    if (!active_) return;
    latest_ = service_->latestSample(); contentToken_ = latest_.sessionToken;
    confirmedTitle_ = latest_.title; confirmedUri_ = latest_.mediaUri; confirmed_ = true;
    recent_.opened(video_);
    automatic_.setFileTitle(QFileInfo(video_).fileName());
    if (restorePosition_ > 0 || intent_ == FromStart) beginRestore(); else prepared();
}
bool MediaSession::sameContent(const MediaSample& sample) const {
    return sample.id == bound_ && sample.sessionToken == contentToken_ && sample.title == confirmedTitle_
        && sample.mediaUri == confirmedUri_;
}
void MediaSession::beginRestore() {
    if (!active_ || !confirmed_ || !latest_.found) return;
    restoreFailed_ = false; restoring_ = true; ready_ = writing_ = false; seekAccepted_ = seekObserved_ = false;
    error_.clear(); status_ = QStringLiteral("正在恢复观看位置…");
    seekRequest_ = nextRequest(); seekTask_ = task_; seekAt_ = MediaMonitor::monotonicSeconds(); seekDeadline_.start();
    emit stateChanged(); emit seekRequested(latest_, restorePosition_, seekRequest_);
}
void MediaSession::restoreError(const QString& error) {
    seekDeadline_.stop(); restoring_ = false; restoreFailed_ = true; writing_ = false;
    error_ = error; status_ = QStringLiteral("恢复位置失败，原观看记录已保留"); emit stateChanged();
}
void MediaSession::retryRestore() { if (restoreFailed_ && active_) beginRestore(); }
void MediaSession::continueAtCurrentPosition() { if (restoreFailed_ && active_) prepared(); }
void MediaSession::prepared() {
    if (!active_ || !confirmed_ || !connected()) { restoreError(QStringLiteral("播放器连接已断开，请重新打开视频")); return; }
    seekDeadline_.stop(); restoring_ = restoreFailed_ = false; ready_ = writing_ = true; error_.clear();
    status_ = QStringLiteral("视频已就绪，正在准备弹幕"); emit stateChanged();
    if (source_.value("kind") == "xml") { loadFile(source_.value("path").toString()); return; }
    if (source_.value("kind") == "disabled") { manualSource_ = true; status_ = QStringLiteral("弹幕已卸载，可手动选源或重新匹配"); return; }
    if (source_.value("kind") == "online") {
        manualSource_ = true; const auto metadata = source_.value("metadata").toMap();
        const auto key = danmakuCacheKey(metadata.value("server").toString(), metadata.value("episodeId").toLongLong());
        if (QFileInfo(QDir(cacheRoot_).filePath("danmaku/" + key + ".json")).isFile()) library_.loadCached(key);
        else library_.loadKnownEpisode(metadata);
        return;
    }
    automatic_.configure(true, servers_); automatic_.observe(latest_);
}
void MediaSession::observe(const MediaSample& sample) {
    if (!active_ || sample.id != bound_ || sample.generation != app_->mediaGeneration_) return;
    const auto age = MediaMonitor::monotonicSeconds() - sample.observedAt;
    if (age < 0 || age > 2.5 || sample.observedAt <= latest_.observedAt) return;
    if (!confirmed_) { service_->observe(sample); return; }
    if (!sample.found) { flush(); automatic_.observe(sample); emit stateChanged(); return; }
    if (sample.playback == MediaSample::Playback::Stopped || sample.playback == MediaSample::Playback::Closed) {
        flush(); latest_.playing = false;
        status_ = QStringLiteral("播放器已停止，观看进度已保留"); emit stateChanged(); return;
    }
    if (!sameContent(sample)) {
        flush(); writing_ = ready_ = active_ = false; seekDeadline_.stop(); cancelLoader(); automatic_.reset(); library_.cancel();
        app_->clearMediaItems(); status_ = QStringLiteral("播放器内容已切换，请从媒体库重新打开视频"); emit stateChanged(); return;
    }
    const bool paused = latest_.playing && !sample.playing;
    latest_ = sample;
    if (restoring_ && sample.observedAt > seekAt_) {
        const auto target = restorePosition_ + (sample.playing ? (sample.observedAt - seekAt_) * sample.rate : 0);
        seekObserved_ = std::abs(sample.position - target) <= 1;
        if (seekObserved_ && seekAccepted_) prepared();
    }
    if (ready_ && !manualSource_) automatic_.observe(sample);
    else automatic_.playbackRecovery()->observe(sample);
    if (paused) flush();
    emit stateChanged();
}
void MediaSession::flush() {
    if (writing_ && confirmed_ && latest_.found) recent_.checkpoint(video_, latest_.position, latest_.duration, source_, contentMetadata_);
}
void MediaSession::deactivate() {
    flush(); ++task_; saveTimer_.stop(); seekDeadline_.stop(); ++seekRequest_;
    cancelLoader(); automatic_.reset(); library_.cancel();
    active_ = confirmed_ = ready_ = writing_ = restoring_ = restoreFailed_ = false;
    status_ = video_.isEmpty() ? QString() : QStringLiteral("弹幕任务已交接，播放器保持原状态"); emit stateChanged();
}
void MediaSession::cancelLoader() {
    ++loadGeneration_; loader_.request_stop(); if (loader_.joinable()) loader_.join(); loading_ = false;
}
void MediaSession::loadFile(const QString& input) {
    if (!active_) {
        const auto path = RecentVideoStore::normalizedPath(input);
        if (!video_.isEmpty() && QFileInfo(path).isFile()) {
            source_ = {{"kind", "xml"}, {"path", path}}; sourceTitle_ = path;
            recent_.checkpoint(video_, latest_.position, latest_.duration, source_);
            status_ = QStringLiteral("已为下次播放选择 XML"); emit stateChanged();
        }
        return;
    }
    if (!ready_) return;
    const auto path = RecentVideoStore::normalizedPath(input); cancelLoader(); automatic_.suspend(false); manualSource_ = true;
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        source_ = {{"kind", "xml"}, {"path", input}};
        error_ = QStringLiteral("指定的 XML 不存在，请重新选择");
        automatic_.playbackRecovery()->begin(latest_, error_); emit stateChanged(); return;
    }
    loading_ = true; error_.clear(); const auto task = task_, generation = loadGeneration_; emit stateChanged();
    auto result = std::make_shared<XmlResult>();
    loader_ = std::jthread([this, path, task, generation, result](std::stop_token stop) {
        *result = readDanmakuXml(path, stop);
        QMetaObject::invokeMethod(this, [this, path, task, generation, result] {
            if (!active_ || task != task_ || generation != loadGeneration_) return;
            loading_ = false;
            if (result->cancelled) return;
            if (!result->error.isEmpty() || result->items.empty()) {
                error_ = result->error.isEmpty() ? QStringLiteral("XML 没有可用弹幕") : result->error;
                automatic_.playbackRecovery()->begin(latest_, error_); emit stateChanged(); return;
            }
            accept(std::move(result->items), path, {{"kind", "xml"}, {"path", path}});
        }, Qt::QueuedConnection);
    });
}
void MediaSession::accept(std::vector<danmaku::Item> items, const QString& title, const QVariantMap& source) {
    if (!active_ || !ready_ || !latest_.found) return;
    if (!connected() || MediaMonitor::monotonicSeconds() - latest_.observedAt > 2.5) {
        error_ = QStringLiteral("播放器连接已断开，未提交弹幕，请重新打开视频"); emit stateChanged(); return;
    }
    total_ = static_cast<int>(items.size()); sourceTitle_ = title; source_ = source;
    app_->presentMedia(std::move(items), title, latest_);
    latest_.generation = app_->mediaGeneration_;
    automatic_.playbackRecovery()->finish(true); status_ = QStringLiteral("弹幕已载入，正在跟随视频"); error_.clear(); flush(); emit stateChanged();
}
void MediaSession::unload() {
    if (!active_) return;
    cancelLoader(); automatic_.suspend(); library_.cancel(); manualSource_ = true;
    automatic_.playbackRecovery()->finish(true);
    total_ = 0; sourceTitle_.clear(); source_ = {{"kind", "disabled"}};
    app_->clearMediaItems(); status_ = QStringLiteral("弹幕已卸载，播放器连接保留"); flush(); emit stateChanged();
}
void MediaSession::stopDanmaku() {
    if (!active_) return;
    cancelLoader(); automatic_.suspend(); manualSource_ = true;
    automatic_.playbackRecovery()->finish(true);
    QScopedValueRollback source(app_->automaticSource_, true);
    app_->resetPlayback(false); status_ = QStringLiteral("弹幕已停止，播放器连接保留"); flush(); emit stateChanged();
}
void MediaSession::retryMatching() {
    if (!active_ || !ready_) return;
    source_.clear(); manualSource_ = false; automatic_.configure(true, servers_); automatic_.retry(); automatic_.observe(latest_);
}
void MediaSession::showAutomaticDanmakuChoice() { automatic_.openRecovery(); }
void MediaSession::searchDanmaku() { if (active_ && ready_) emit requestOnlineDanmakuSearch(); }
void MediaSession::copyRecoveryTitle() { if (auto* clipboard = QGuiApplication::clipboard()) clipboard->setText(automatic_.playbackRecovery()->title()); }
