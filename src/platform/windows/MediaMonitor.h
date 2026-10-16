#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include "infrastructure/MediaPlaybackBackend.h"
class SmtcBackend;
class VlcHttpBackend;
class MediaMonitor final : public QObject {
    Q_OBJECT
  public:
    explicit MediaMonitor(QObject* parent = nullptr, const QString& playerDirectory = {});
    ~MediaMonitor() override;
    quint64 select(QString id, bool renew = false);
    quint64 generation();
    void requestPlayback(MediaSample expected, bool play, quint64 request);
    ManagedPlaybackOpen openManaged(const QString& adapter, const QString& executable, const QString& video);
    void cancelManagedOpen();
    static double monotonicSeconds();
    static bool targetForeground(const QString& applicationId);
    static bool windowMatchesSession(quintptr handle, const QString& id);
    static QVariantMap processMetrics();
    static bool windowOrOwnerForeground(quintptr handle);
    static QVariantMap windowMetrics(quintptr handle);
  signals:
    void sessionsChanged(QVariantList sessions);
    void sampleReceived(MediaSample sample);
    void errorOccurred(QString error);
    void playbackControlFinished(quint64 request, bool success, QString error);

  private:
    void publishSessions();
    SmtcBackend* smtc_{};
    VlcHttpBackend* vlc_{};
    MediaPlaybackBackend* active_{};
    QVariantList smtcSessions_, vlcSessions_;
    QString selected_;
    quint64 generation_{1};
};
