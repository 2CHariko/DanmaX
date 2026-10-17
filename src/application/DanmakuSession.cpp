#include "application/DanmakuSession.h"
#include "application/AppController.h"
#include "infrastructure/XmlLoader.h"
#include <QFileInfo>
#include <QUrl>
#include <algorithm>
#include <memory>
DanmakuSession::DanmakuSession(const QString& data, const QString& cache, AppController* app)
    : QObject(app), app_(app), library_(cache, this) {
    Q_UNUSED(data);
    connect(this, &DanmakuSession::stateChanged, this, &DanmakuSession::changed);
    connect(&library_, &DanmakuLibrary::operationStarted, this, [this] {
        cancelXml(); libraryTask_ = task_; error_.clear(); emit stateChanged();
    });
    connect(&library_, &DanmakuLibrary::changed, this, [this] {
        if (active() && !library_.busy() && !library_.error().isEmpty()) {
            error_ = library_.error(); emit sourceFailed(error_);
        }
        emit stateChanged();
    });
    connect(&library_, &DanmakuLibrary::sourceReady, this, [this] {
        const auto result = library_.takeReadyResult();
        if (!result || !active() || task_ != libraryTask_) return;
        const auto title = result->metadata.value("animeTitle").toString() + QStringLiteral(" · ") + result->metadata.value("episodeTitle").toString();
        accept(std::move(result->items), title, result->metadata, {}, false);
    });
}
DanmakuSession::~DanmakuSession() { disconnect(&library_, nullptr, this, nullptr); cancelXml(); library_.cancel(); }
bool DanmakuSession::active() const { return productActive_ && app_->coordinator_.owner() == PlaybackCoordinator::Danmaku; }
bool DanmakuSession::loading() const { return preparing_ || app_->loading_; }
int DanmakuSession::loadProgress() const { return app_->progress_; }
QString DanmakuSession::status() const { return running() ? app_->status_ : status_; }
bool DanmakuSession::running() const { return app_->coordinator_.owner() == PlaybackCoordinator::Danmaku && app_->running_; }
bool DanmakuSession::playing() const { return running() && app_->playing_; }
bool DanmakuSession::completed() const { return running() && app_->completed(); }
double DanmakuSession::position() const { return running() ? app_->position_ : 0; }
double DanmakuSession::duration() const { return running() ? app_->duration_ : items_.empty() ? 0 : items_.back().time + 15; }
QString DanmakuSession::mediaTitle() const { return running() ? app_->mediaTitle_ : QString(); }
bool DanmakuSession::connected() const { return app_->coordinator_.owner() != PlaybackCoordinator::Media && app_->targetSessionConnected_; }
void DanmakuSession::start(bool manual) { app_->start(manual); }
void DanmakuSession::stop() { if (app_->coordinator_.owner() == PlaybackCoordinator::Danmaku) app_->stop(); }
void DanmakuSession::unload() {
    if (!active()) { app_->unloadDanmaku(); return; }
    cancelPreparation(); ++task_; app_->resetPlayback(true);
    items_.clear(); metadata_.clear(); title_.clear(); file_.clear(); error_.clear();
    status_ = QStringLiteral("弹幕已卸载，请选择新的来源"); emit stateChanged();
}
void DanmakuSession::togglePause() { if (running()) app_->togglePause(); }
void DanmakuSession::seek(double position) { if (running()) app_->seek(position); }
void DanmakuSession::configure(const QStringList& servers) { library_.setServers(servers); }
void DanmakuSession::cancelXml() {
    ++request_; loader_.request_stop(); if (loader_.joinable()) loader_.join(); preparing_ = false;
}
void DanmakuSession::cancelPreparation() { cancelXml(); library_.cancel(); emit stateChanged(); }
void DanmakuSession::deactivate() {
    cancelPreparation(); ++task_;
    if (productActive_) { items_.clear(); metadata_.clear(); title_.clear(); file_.clear(); error_.clear(); }
    productActive_ = false; emit stateChanged();
}
void DanmakuSession::openFile(const QString& file) { prepareXml(file, true); }
void DanmakuSession::loadFile(const QString& file) { if (active()) prepareXml(file, false); }
void DanmakuSession::prepareXml(const QString& input, bool opening) {
    const QUrl url(input); const auto path = url.isLocalFile() ? url.toLocalFile() : input;
    if (path.isEmpty()) return;
    cancelXml(); library_.cancel(); error_.clear(); preparing_ = true;
    const auto request = request_, task = task_, owner = app_->coordinator_.generation();
    emit stateChanged();
    loader_ = std::jthread([this, path, opening, request, task, owner](std::stop_token stop) {
        auto result = std::make_shared<XmlResult>(readDanmakuXml(path, stop));
        QMetaObject::invokeMethod(this, [this, path, opening, request, task, owner, result] {
            if (request != request_ || task != task_ || owner != app_->coordinator_.generation()) return;
            preparing_ = false;
            if (result->cancelled) { emit stateChanged(); return; }
            if (!result->error.isEmpty() || result->items.empty()) {
                error_ = result->error.isEmpty() ? QStringLiteral("XML 没有可播放的弹幕") : result->error;
                emit sourceFailed(error_); emit stateChanged(); return;
            }
            accept(std::move(result->items), path, {}, path, opening);
        }, Qt::QueuedConnection);
    });
}
void DanmakuSession::accept(std::vector<danmaku::Item> items, const QString& title, const QVariantMap& metadata, const QString& file, bool opening) {
    if (items.empty() || (!opening && !active())) return;
    const bool replacing = !opening && active() && running();
    const bool paused = replacing && !playing();
    const double at = replacing ? position() : 0;
    if (opening) app_->handoff(PlaybackCoordinator::Danmaku);
    else cancelPreparation();
    productActive_ = manual_ = true; ++task_;
    items_ = std::move(items); metadata_ = metadata; title_ = title; file_ = file; error_.clear();
    app_->file_ = file;
    app_->commitItems(items_, title_); app_->start(true);
    if (paused) app_->togglePause();
    if (at > 0) app_->seek(std::min(at, duration()));
    if (!file.isEmpty()) app_->settings_.setValue("lastFile", file);
    status_ = QStringLiteral("独立弹幕已载入");
    emit stateChanged(); emit sourceCommitted(); emit app_->requestNavigateToPage(1, -1);
}
void DanmakuSession::loadCached(const QString& key) { if (active() && !key.isEmpty()) library_.loadCached(key); }
void DanmakuSession::searchDanmaku() { if (active()) emit requestOnlineDanmakuSearch(); }
void DanmakuSession::searchForAnime(const QString& title, const QString& episodeTitle, double episodeNumber) {
    if (!active()) return;
    emit requestOnlineDanmakuSearch();
    if (episodeNumber > 0 || !episodeTitle.trimmed().isEmpty()) library_.searchAnimeForEpisode(title, episodeTitle, episodeNumber);
    else if (!title.trimmed().isEmpty()) library_.searchAnime(title);
}
