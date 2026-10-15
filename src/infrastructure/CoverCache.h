#pragma once
#include "infrastructure/MetadataCache.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QQueue>
#include <QThreadPool>
#include <memory>

class CoverCache;
class CoverRequest final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QUrl url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(QUrl source READ source NOTIFY sourceChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
public:
    CoverRequest(CoverCache* service, QObject* parent);
    ~CoverRequest() override;
    QUrl url() const { return url_; }
    QUrl source() const { return source_; }
    bool loading() const { return loading_; }
    void setUrl(const QUrl& url);
    Q_INVOKABLE void retry();
signals:
    void urlChanged();
    void sourceChanged();
    void loadingChanged();
private:
    friend class CoverCache;
    void setSource(const QUrl& url);
    void setLoading(bool loading);
    QPointer<CoverCache> service_;
    QUrl url_, source_;
    bool loading_{};
};

class CoverCache final : public QObject {
    Q_OBJECT
public:
    explicit CoverCache(MetadataCache* store, QObject* parent = nullptr, int timeoutMs = 15000);
    ~CoverCache() override;
    Q_INVOKABLE QObject* createRequest(QObject* owner);
    void attach(CoverRequest* request);
    void detach(CoverRequest* request);
    void refresh(CoverRequest* request);
    void reset();
private:
    struct Entry {
        QString key;
        QUrl url;
        QList<QPointer<CoverRequest>> watchers;
        QPointer<QNetworkReply> reply;
        MetadataCache::Record cached;
        bool active = true;
        bool finished = false;
        bool downloading = false;
        quint64 lastUse{};
        qint64 retainedBytes{};
    };
    void pins();
    void trimIdle();
    void setLoading(const std::shared_ptr<Entry>& entry, bool loading);
    void enqueue(const std::shared_ptr<Entry>& entry);
    void pump();
    void download(const std::shared_ptr<Entry>& entry);
    void complete(const std::shared_ptr<Entry>& entry, QUrl source = {}, QDateTime fetchedAt = {});
    MetadataCache* store_;
    QNetworkAccessManager network_;
    QThreadPool worker_;
    QHash<QString, std::shared_ptr<Entry>> entries_;
    QQueue<std::shared_ptr<Entry>> pending_;
    QHash<QString, QDateTime> failures_;
    QList<QPointer<CoverRequest>> requests_;
    int active_ = 0;
    quint64 useCounter_{};
    int timeoutMs_;
};
