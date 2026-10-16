#pragma once
#include "application/AutomaticDanmaku.h"
#include "application/VideoOpenService.h"
#include "infrastructure/RecentVideoStore.h"
#include <QTimer>
#include <thread>
class AppController;
class ExternalPlayerStore;

class MediaSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* recent READ recent CONSTANT)
    Q_PROPERTY(QObject* automatic READ automatic CONSTANT)
    Q_PROPERTY(QObject* library READ library CONSTANT)
    Q_PROPERTY(QObject* videoOpen READ videoOpen CONSTANT)
    Q_PROPERTY(QString videoPath READ videoPath NOTIFY stateChanged)
    Q_PROPERTY(QString title READ title NOTIFY stateChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString sourceTitle READ sourceTitle NOTIFY stateChanged)
    Q_PROPERTY(int total READ total NOTIFY stateChanged)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(bool restoring READ restoring NOTIFY stateChanged)
    Q_PROPERTY(bool restoreFailed READ restoreFailed NOTIFY stateChanged)
    Q_PROPERTY(bool canSelectSource READ canSelectSource NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY stateChanged)
    Q_PROPERTY(double duration READ duration NOTIFY stateChanged)
    Q_PROPERTY(bool targetSessionConnected READ connected NOTIFY stateChanged)
    Q_PROPERTY(bool manualMode READ manualMode CONSTANT)
  public:
    enum Intent { Normal, Continue, FromStart };
    Q_ENUM(Intent)
    MediaSession(const QString& data, const QString& cache, AppController* app, VideoOpenService* service,
                 ExternalPlayerStore* players, MediaMonitor* monitor);
    ~MediaSession() override;
    QObject* recent() { return &recent_; }
    QObject* automatic() { return &automatic_; }
    QObject* library() { return &library_; }
    QObject* videoOpen() { return service_; }
    QString videoPath() const { return video_; }
    QString title() const;
    QString status() const { return status_; }
    QString error() const { return error_; }
    QString sourceTitle() const { return sourceTitle_; }
    int total() const { return total_; }
    bool active() const { return active_; }
    bool loading() const { return loading_; }
    bool restoring() const { return restoring_; }
    bool restoreFailed() const { return restoreFailed_; }
    bool canSelectSource() const { return active_ ? ready_ : !video_.isEmpty(); }
    double position() const { return latest_.position; }
    double duration() const { return latest_.duration; }
    bool connected() const;
    bool manualMode() const { return false; }
    QString boundSession() const { return bound_; }
    Q_INVOKABLE void openVideo(const QString& file, int intent = Normal);
    Q_INVOKABLE void retryRestore();
    Q_INVOKABLE void continueAtCurrentPosition();
    Q_INVOKABLE void loadFile(const QString& file);
    Q_INVOKABLE void unload();
    Q_INVOKABLE void stopDanmaku();
    Q_INVOKABLE void retryMatching();
    Q_INVOKABLE void showAutomaticDanmakuChoice();
    Q_INVOKABLE void searchDanmaku();
    Q_INVOKABLE void copyRecoveryTitle();
    void observe(const MediaSample& sample);
    void deactivate();
    void flush();
    void configure(const QStringList& servers);
  signals:
    void stateChanged();
    void requestOnlineDanmakuSearch();
    void seekRequested(MediaSample expected, double seconds, quint64 request);
  private:
    friend class AppController;
    void beginTask(const QString& path);
    void confirmed();
    void beginRestore();
    void restoreError(const QString& error);
    void prepared();
    void accept(std::vector<danmaku::Item> items, const QString& title, const QVariantMap& source);
    void cancelLoader();
    bool sameContent(const MediaSample& sample) const;
    AppController* app_;
    VideoOpenService* service_;
    ExternalPlayerStore* players_;
    MediaMonitor* monitor_;
    RecentVideoStore recent_;
    AutomaticDanmaku automatic_;
    DanmakuLibrary library_;
    QString video_, bound_, status_, error_, sourceTitle_, confirmedTitle_, confirmedUri_, cacheRoot_;
    QVariantMap source_;
    QStringList servers_;
    MediaSample latest_;
    QTimer seekDeadline_, saveTimer_;
    std::jthread loader_;
    quint64 task_{}, loadGeneration_{}, contentToken_{}, seekRequest_{}, seekTask_{};
    double restorePosition_{}, seekAt_{};
    int intent_{Normal}, total_{};
    bool active_{}, confirmed_{}, ready_{}, loading_{}, restoring_{}, restoreFailed_{}, seekAccepted_{}, seekObserved_{}, writing_{}, manualSource_{}, opening_{}, starting_{};
};
