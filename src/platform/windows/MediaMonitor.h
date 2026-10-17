#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include "infrastructure/MediaPlaybackBackend.h"
class VlcHttpBackend;
class PotPlayerBackend;
class LibVlcBackend;
class MediaMonitor final : public QObject {
    Q_OBJECT
  public:
    explicit MediaMonitor(QObject* parent = nullptr, const QString& playerDirectory = {});
    ~MediaMonitor() override;
    quint64 select(QString id, bool renew = false);
    quint64 generation();
    void requestPlayback(MediaSample expected, bool play, quint64 request);
    void requestSeek(MediaSample expected, double seconds, quint64 request);
    ManagedPlaybackOpen openManaged(const QString& adapter, const QString& executable, const QString& video);
    void cancelManagedOpen();
    void closeOwnedPlayer(const QString& session);
    bool closesWithTask(const QString& session) const;
    bool embedded(const QString& session) const;
    bool embeddedBusy() const;
    void setVideoOutput(quintptr handle);
    QVariantMap embeddedMetrics() const;
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
    void seekFinished(quint64 request, bool success, QString error);
    void seekDispatched(SeekDispatch result);
    void ownedPlayerExited(QString session);
    void ownedPlayerCloseFailed(QString error);
    void embeddedActivityChanged();

  private:
    void publishSessions();
    VlcHttpBackend* vlc_{};
    PotPlayerBackend* pot_{};
    LibVlcBackend* embedded_{};
    MediaPlaybackBackend* active_{};
    QList<MediaPlaybackBackend*> backends_;
    QVariantList vlcSessions_;
    QString selected_;
    quint64 generation_{1};
};
