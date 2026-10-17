#pragma once
#include "infrastructure/MediaPlaybackBackend.h"
#include "platform/windows/PlayerProcessIdentity.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrlQuery>
#include <deque>
#include <functional>
#include <thread>
#include <atomic>
#include <vector>
#include <memory>
class QNetworkReply;
class VlcHttpBackend final : public MediaPlaybackBackend {
    Q_OBJECT
  public:
    struct Hooks {
        std::function<ManagedPlaybackOpen(const QString&, const QStringList&, const QMap<QString, QString>&)> launch;
        std::function<bool(const QString&, const QString&)> alive;
        std::function<PlayerProcessIdentity::Listener(quint16, const QString&)> listener;
        std::function<QVariantList()> discover;
        std::function<QString(const QString&, const QString&)> close;
    };
    explicit VlcHttpBackend(QString directory, QObject* parent = nullptr, Hooks hooks = {});
    ~VlcHttpBackend() override;
    QString adapterId() const override { return QStringLiteral("vlc"); }
    bool accepts(const QString& id) const override { return id.startsWith("danmax-vlc:"); }
    ManagedPlaybackOpen open(const QString& executable, const QString& video) override;
    void select(const QString& id, quint64 generation) override;
    void requestPlayback(MediaSample expected, bool play, quint64 request) override;
    void requestSeek(MediaSample expected, double seconds, quint64 request) override;
    void cancelOpen() override;
    bool closesWithTask() const override { return true; }
    void closeOwned(const QString& id = {}) override;
    QString sessionId() const { return session_; }
  signals:
    void ownedExited(QString session);
    void closeFailed(QString error);
  private:
    bool alive() const;
    void invalidate();
    void poll();
    void missing();
    void unavailable(const QString& error);
    void request(QUrlQuery query, const QString& kind, quint64 control = 0);
    void publishSessions();
    QVariantList discovered_, published_;
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
    struct Command { MediaSample expected; bool play{}; quint64 request{}; double seconds{-1}; bool seek{}; };
    std::deque<Command> commands_;
    Hooks hooks_;
    QString lastError_;
    std::jthread discovery_;
    struct Closing {
        QString id;
        std::shared_ptr<std::atomic_bool> done;
        std::jthread worker;
    };
    std::vector<Closing> closing_;
};
