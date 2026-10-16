#pragma once
#include "platform/windows/MediaMonitor.h"
#include "infrastructure/ExternalPlayer.h"
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <functional>

class VideoOpenService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString playerPath READ playerPath NOTIFY changed)
    Q_PROPERTY(QString playerName READ playerName NOTIFY changed)
    Q_PROPERTY(bool canDiscover READ canDiscover NOTIFY changed)
    Q_PROPERTY(bool synchronizes READ synchronizes NOTIFY changed)
    Q_PROPERTY(QString executableFilter READ executableFilter NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool needsPlayer READ needsPlayer NOTIFY changed)
    Q_PROPERTY(bool needsConfirmation READ needsConfirmation NOTIFY changed)
  public:
    struct Hooks {
        std::function<QString()> discover;
        std::function<QString(const QString&)> preflight;
        std::function<QString(const QString&, const QStringList&)> launch;
    };
    explicit VideoOpenService(QObject* parent = nullptr, Hooks hooks = {}, int timeoutMs = 15000,
                              ExternalPlayerRegistry registry = {});
    QString playerPath() const { return playerPath_; }
    QString playerName() const { return profile_.name; }
    bool canDiscover() const { return adapter() && adapter()->canDiscover(); }
    bool synchronizes() const { return adapter() && (adapter()->managedPlayback() || !adapter()->sessionId(profile_).isEmpty()); }
    QString executableFilter() const;
    QString status() const { return status_; }
    bool pending() const { return pending_; }
    bool active() const { return active_; }
    bool needsPlayer() const { return needsPlayer_; }
    bool needsConfirmation() const;
    QString currentVideo() const { return video_; }
    MediaSample latestSample() const { return latest_; }
    QString sessionId() const { return transactionSession_; }
    qint64 launchedPid() const { return launchedPid_; }
    void configurePath(const QString& path);
    void configureProfile(const ExternalPlayerProfile& profile);
    void observe(const MediaSample& sample);
    Q_INVOKABLE bool accepts(const QVariantList& files) const;
    Q_INVOKABLE QString displayName(const QVariantList& files) const;
    Q_INVOKABLE void open(const QVariantList& files);
    Q_INVOKABLE void choosePlayer(const QString& path);
    Q_INVOKABLE void discoverPlayer();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void confirmCurrent();
    void backendPrepared(const QString& sessionId);
    void backendFailed(const QString& error);
  signals:
    void changed();
    void playerPathSelected(QString path);
    void playerRequired();
    void opening(QString sessionId);
    void confirmed();
    void cancelled();
    void failed(QString error);
    void launchOnlyOpening();
    void validated(QString video);
    void managedOpenRequested(QString adapter, QString executable, QString video);
    void managedOpenCancelled();
  private:
    static QString videoPath(const QVariantList& files);
    bool matchesTarget(const MediaSample& sample) const;
    void fail(const QString& error);
    const IExternalPlayerAdapter* adapter() const { return registry_.find(profile_.adapter); }
    QString preflight() const;
    QString launch();
    ExternalPlayerRegistry registry_;
    ExternalPlayerProfile profile_;
    Hooks hooks_;
    QTimer deadline_;
    QString playerPath_, status_, video_, waitingFile_, baselineTitle_, transactionSession_;
    MediaSample latest_;
    double openedAt_{};
    qint64 launchedPid_{};
    bool pending_{}, active_{}, needsPlayer_{}, awaitingBaseline_{};
};
