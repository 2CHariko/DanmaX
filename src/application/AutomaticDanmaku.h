#pragma once
#include "application/DanmakuLibrary.h"
#include "infrastructure/MediaAssociationStore.h"
#include "platform/windows/MediaMonitor.h"
#include "application/PlaybackRecovery.h"
#include "infrastructure/MediaTitleParser.h"
#include <QTimer>

class AutomaticDanmaku final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* library READ library CONSTANT)
    Q_PROPERTY(QObject* recovery READ recovery CONSTANT)
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool suspended READ suspended NOTIFY changed)
    Q_PROPERTY(bool needsChoice READ needsChoice NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString recognizedTitle READ recognizedTitle NOTIFY changed)
  public:
    AutomaticDanmaku(const QString& dataDirectory, const QString& cacheRoot, QObject* parent = nullptr, int settleMs = 600);
    QObject* library() { return &library_; }
    QObject* recovery() { return &recovery_; }
    PlaybackRecovery* playbackRecovery() { return &recovery_; }
    bool enabled() const { return enabled_; }
    bool suspended() const { return suspended_; }
    bool needsChoice() const { return phase_ == Choice; }
    QString status() const { return status_; }
    QString recognizedTitle() const;
    QString contentIdentity() const { return identity_; }
    void configure(bool enabled, const QStringList& servers);
    void observe(const MediaSample& sample);
    void reset(bool invalidateRecovery = true);
    Q_INVOKABLE void suspend();
    void refreshCache() { library_.refreshCache(); }
    Q_INVOKABLE void retry();
    Q_INVOKABLE void forgetAssociation();
    Q_INVOKABLE void cancelRecovery();
    Q_INVOKABLE void openRecovery();
    std::shared_ptr<OnlineDanmakuResult> takeReadyResult();
    MediaSample latestSample() const { return latest_; }
  signals:
    void changed();
    void contentInvalidated();
    void sourceReady();
  private:
    enum Phase { Waiting, Settling, Cache, LoadingCache, Searching, Choice, Loaded, Failed, Skipped };
    void offerRecovery();
    void resolve();
    void search(bool requireChoice = false);
    void libraryChanged();
    void accept();
    bool fresh() const;
    void state(Phase phase, const QString& status);
    DanmakuLibrary library_;
    PlaybackRecovery recovery_{this};
    MediaAssociationStore associations_;
    QTimer settle_;
    MediaSample latest_;
    ParsedMediaTitle parsed_;
    MediaParseResult parseResult_;
    QString offeredIdentity_;
    QString identity_, status_;
    QStringList servers_;
    Phase phase_{Waiting};
    bool enabled_{}, suspended_{}, changing_{};
    std::shared_ptr<OnlineDanmakuResult> ready_;
};
