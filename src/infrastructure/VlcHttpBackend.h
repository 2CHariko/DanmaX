#pragma once
#include "infrastructure/MediaPlaybackBackend.h"
#include "platform/windows/PlayerProcessIdentity.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>
#include <deque>
#include <functional>
class QNetworkReply;
class VlcHttpBackend final : public MediaPlaybackBackend {
    Q_OBJECT
  public:
    struct Hooks {
        std::function<ManagedPlaybackOpen(const QString&, const QStringList&, const QMap<QString, QString>&)> launch;
        std::function<bool(const QString&, const QString&)> alive;
        std::function<PlayerProcessIdentity::Listener(quint16, const QString&)> listener;
    };
    explicit VlcHttpBackend(QString directory, QObject* parent = nullptr, Hooks hooks = {});
    ~VlcHttpBackend() override;
    bool accepts(const QString& id) const override { return id.startsWith("danmax-vlc:"); }
    ManagedPlaybackOpen open(const QString& executable, const QString& video);
    void select(const QString& id, quint64 generation) override;
    void requestPlayback(MediaSample expected, bool play, quint64 request) override;
    void cancelOpen();
    QString sessionId() const { return session_; }
  private:
    bool alive() const;
    void invalidate();
    void poll();
    void missing();
    void unavailable(const QString& error);
    void request(QUrlQuery query, const QString& kind, quint64 control = 0);
    QString directory_, executable_, session_, selected_, password_, pendingOpen_, latestItem_;
    QString openingTarget_, openedFromItem_, operation_;
    quint16 port_{};
    quint64 generation_{}, epoch_{}, sequence_{};
    QTimer timer_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QByteArray body_;
    MediaSample latest_;
    double lastRawChangedAt_{};
    struct Command { MediaSample expected; bool play{}; quint64 request{}; };
    std::deque<Command> commands_;
    Hooks hooks_;
    QString lastError_;
};
