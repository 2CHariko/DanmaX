#pragma once
#include "platform/windows/MediaMonitor.h"
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <functional>

class VideoOpenService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString playerPath READ playerPath NOTIFY changed)
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
    explicit VideoOpenService(QObject* parent = nullptr, Hooks hooks = {}, int timeoutMs = 15000);
    QString playerPath() const { return playerPath_; }
    QString status() const { return status_; }
    bool pending() const { return pending_; }
    bool active() const { return active_; }
    bool needsPlayer() const { return needsPlayer_; }
    bool needsConfirmation() const;
    void configurePath(const QString& path);
    void observe(const MediaSample& sample);
    Q_INVOKABLE bool accepts(const QVariantList& files) const;
    Q_INVOKABLE QString displayName(const QVariantList& files) const;
    Q_INVOKABLE void open(const QVariantList& files);
    Q_INVOKABLE void choosePlayer(const QString& path);
    Q_INVOKABLE void discoverPlayer();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void confirmCurrent();
  signals:
    void changed();
    void playerPathSelected(QString path);
    void playerRequired();
    void opening(QString sessionId);
    void confirmed();
    void cancelled();
    void failed(QString error);
  private:
    static QString videoPath(const QVariantList& files);
    bool matchesTarget(const MediaSample& sample) const;
    void fail(const QString& error);
    Hooks hooks_;
    QTimer deadline_;
    QString playerPath_, status_, video_, waitingFile_, baselineTitle_;
    MediaSample latest_;
    double openedAt_{};
    bool pending_{}, active_{}, needsPlayer_{}, awaitingBaseline_{};
};
