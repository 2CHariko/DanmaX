#pragma once
#include "infrastructure/MediaPlaybackBackend.h"
#include <QTimer>
#include <QVariantMap>
class AppController;

// The UI's single playback surface. The workflow owners retain their own IO,
// matching and cancellation; this facade never owns a second playback clock.
class NowPlayingModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool video READ video NOTIFY changed)
    Q_PROPERTY(bool standalone READ standalone NOTIFY changed)
    Q_PROPERTY(bool openingStandalone READ openingStandalone NOTIFY changed)
    Q_PROPERTY(bool canSelectSource READ canSelectSource NOTIFY changed)
    Q_PROPERTY(QObject* sourceSession READ sourceSession NOTIFY changed)
    Q_PROPERTY(quint64 contextId READ contextId NOTIFY changed)
    Q_PROPERTY(bool ownsPlayer READ ownsPlayer NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canPlay READ canPlay NOTIFY changed)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString subtitle READ subtitle NOTIFY changed)
    Q_PROPERTY(QString coverUrl READ coverUrl NOTIFY changed)
    Q_PROPERTY(QString source READ source NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString controlHint READ controlHint NOTIFY changed)
    Q_PROPERTY(int count READ count NOTIFY changed)
    Q_PROPERTY(double position READ position NOTIFY changed)
    Q_PROPERTY(double duration READ duration NOTIFY changed)
  public:
    explicit NowPlayingModel(AppController* app);
    bool video() const;
    bool standalone() const;
    bool openingStandalone() const;
    bool canSelectSource() const;
    QObject* sourceSession() const;
    quint64 contextId() const;
    bool ownsPlayer() const;
    bool active() const;
    bool playing() const;
    bool busy() const;
    bool canPlay() const;
    bool canSeek() const;
    QString title() const;
    QString subtitle() const;
    QString coverUrl() const;
    QString source() const;
    QString status() const;
    QString error() const;
    QString controlHint() const;
    int count() const;
    double position() const;
    double duration() const;
    Q_INVOKABLE void togglePlayback();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void endPlayback();
    Q_INVOKABLE void openStandalone(const QString& path);
    Q_INVOKABLE void cancelStandaloneOpen();
    Q_INVOKABLE void replaceDanmaku(const QString& path);
    Q_INVOKABLE void unloadDanmaku();
    Q_INVOKABLE void searchDanmaku();
    void observe(const MediaSample& sample);
  signals:
    void changed();
    void playbackRequested(MediaSample expected, bool play, quint64 request);
    void seekRequested(MediaSample expected, double seconds, quint64 request);
  private:
    bool external() const;
    bool fresh() const;
    bool same(const MediaSample& sample) const;
    QVariantMap metadata() const;
    void refresh();
    void request(bool play, double seconds = -1);
    void finish(bool success, const QString& error);
    AppController* app_;
    MediaSample sample_, expected_;
    QTimer deadline_;
    QString error_;
    quint64 request_{}, ownerGeneration_{}, displayGeneration_{};
    bool pending_{}, accepted_{}, observed_{}, desiredPlay_{};
    double desiredPosition_{-1}, requestedAt_{};
};
