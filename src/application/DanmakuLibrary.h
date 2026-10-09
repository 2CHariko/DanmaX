#pragma once
#include "infrastructure/OnlineDanmakuData.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QThreadPool>
#include <functional>
#include <memory>
#include <stop_token>

class DanmakuLibrary final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList servers READ servers NOTIFY changed)
    Q_PROPERTY(QString activeServer READ activeServer NOTIFY changed)
    Q_PROPERTY(QVariantList animes READ animes NOTIFY changed)
    Q_PROPERTY(QVariantList episodes READ episodes NOTIFY changed)
    Q_PROPERTY(QVariantList cachedEntries READ cachedEntries NOTIFY cacheChanged)
    Q_PROPERTY(QString selectedAnime READ selectedAnime NOTIFY changed)
    Q_PROPERTY(QString selectedEpisode READ selectedEpisode NOTIFY changed)
    Q_PROPERTY(bool selectedCached READ selectedCached NOTIFY cacheChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool scanning READ scanning NOTIFY cacheChanged)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    explicit DanmakuLibrary(QString cacheRoot, QObject* parent = nullptr, int timeoutMs = 30000);
    ~DanmakuLibrary() override;
    QStringList servers() const { return servers_; }
    QString activeServer() const { return server_; }
    QVariantList animes() const { return animes_; }
    QVariantList episodes() const { return episodes_; }
    QVariantList cachedEntries() const { return entries_; }
    QString selectedAnime() const { return animeId_; }
    QString selectedEpisode() const { return episodeId_; }
    bool selectedCached() const;
    bool busy() const { return busy_; }
    bool scanning() const { return scanning_; }
    int progress() const { return progress_; }
    QString status() const { return status_; }
    QString error() const { return error_; }
    void setServers(const QStringList& addresses);
    Q_INVOKABLE void searchAnime(const QString& keyword);
    Q_INVOKABLE void selectAnime(const QString& id);
    Q_INVOKABLE void selectEpisode(const QString& id);
    Q_INVOKABLE void downloadEpisode(bool refresh = false);
    Q_INVOKABLE void loadCached(const QString& key);
    Q_INVOKABLE void removeCached(const QString& key);
    Q_INVOKABLE void refreshCache();
    Q_INVOKABLE void cancel();
    std::shared_ptr<OnlineDanmakuResult> takeReadyResult();
  signals:
    void changed();
    void cacheChanged();
    void sourceLoadStarted();
    void sourceReady();
  private:
    struct Work {
        QVariantList list;
        std::shared_ptr<OnlineDanmakuResult> result;
        QString error;
    };
    void begin(const QString& status, bool sourceLoad = false);
    void fail(const QString& error);
    void startAttempts(std::function<void()> attempt);
    void nextAttempt(const QString& reason = {});
    void request(const QString& path, const QString& query,
                 std::function<void(QByteArray)> completed);
    void work(std::function<Work(std::stop_token)> action, std::function<void(Work)> completed);
    void fetchEpisode(QVariantMap metadata);
    void acceptResult(std::shared_ptr<OnlineDanmakuResult> result);
    QVariantMap episodeMetadata() const;
    void invalidate();
    QString directory_, server_, animeId_, episodeId_, status_, error_;
    QStringList servers_, attemptErrors_;
    qsizetype attemptIndex_{-1};
    QString operationStatus_;
    std::function<void()> attempt_;
    QVariantList animes_, episodes_, entries_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    QTimer deadline_;
    int timeoutMs_, progress_{-1};
    bool busy_{}, scanning_{};
    quint64 generation_{}, scanGeneration_{};
    QThreadPool worker_, scanner_;
    std::stop_source workerStop_, scannerStop_;
    std::shared_ptr<Work> pending_;
    std::shared_ptr<QVariantList> pendingScan_;
    std::shared_ptr<OnlineDanmakuResult> ready_;
};
