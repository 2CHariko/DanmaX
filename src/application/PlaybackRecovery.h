#pragma once
#include "platform/windows/MediaMonitor.h"
#include <QObject>
#include <QTimer>

// Per-content recovery transaction. Transport is injected through request/result signals.
class PlaybackRecovery final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString reason READ reason NOTIFY changed)
    Q_PROPERTY(QString pauseStatus READ pauseStatus NOTIFY changed)
public:
    explicit PlaybackRecovery(QObject* parent = nullptr);
    bool active() const { return active_; }
    QString title() const { return origin_.title; }
    QString reason() const { return reason_; }
    QString pauseStatus() const { return status_; }
    QString controlWarning() const { return warning_ ? status_ : QString(); }
    bool resumeInFlight() const { return pending_ && playingCommand_; }
    bool resuming() const { return resumeWanted_ || resumeInFlight(); }
    void prepare(const MediaSample& sample);
    void dismissPrompt();
    void begin(const MediaSample& sample, const QString& reason);
    void observe(const MediaSample& sample);
    void finish(bool resume);
    void manualPlaybackRequested();
    void invalidate();
    void controlFinished(quint64 request, bool success, const QString& error);
signals:
    void changed();
    void showRequested();
    void controlRequested(MediaSample expected, bool play, quint64 request);
private:
    bool same(const MediaSample& sample) const;
    void confirmControl();
    void resumeIfOwned();
    MediaSample origin_, latest_;
    QTimer deadline_;
    quint64 request_{};
    double requestedAt_{};
    bool active_{}, preparing_{}, pending_{}, acknowledged_{}, owned_{}, resumeWanted_{}, playingCommand_{}, warning_{}, manualOverride_{};
    QString reason_, status_;
};
