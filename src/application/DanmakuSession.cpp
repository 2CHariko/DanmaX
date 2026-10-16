#include "application/DanmakuSession.h"
#include "application/AppController.h"
DanmakuSession::DanmakuSession(const QString& data, const QString& cache, AppController* app)
    : QObject(app), app_(app), library_(cache, this), automatic_(data, cache, this) {}
bool DanmakuSession::loading() const { return app_->loading_; }
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
void DanmakuSession::unload() { app_->unloadDanmaku(); }
void DanmakuSession::togglePause() { if (running()) app_->togglePause(); }
void DanmakuSession::seek(double position) { if (running()) app_->seek(position); }
