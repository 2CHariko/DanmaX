#pragma once
#include "infrastructure/MetadataCache.h"
#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTimer>
#include <QThreadPool>
#include <QVariantList>
#include <QVariantMap>
#include <QJsonArray>
#include <QDate>
#include <memory>
#include <algorithm>
#include <stop_token>

class CoverCache;
class AnimeMetadataService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QVariantList airingToday READ airingToday NOTIFY calendarChanged)
    Q_PROPERTY(QVariantList weeklyCalendar READ weeklyCalendar NOTIFY calendarChanged)
    Q_PROPERTY(QVariantList topRated READ topRated NOTIFY calendarChanged)
    Q_PROPERTY(QVariantMap heroAnime READ heroAnime NOTIFY calendarChanged)
    Q_PROPERTY(int todayWeekday READ todayWeekday NOTIFY calendarChanged)
    Q_PROPERTY(QVariantList homeWeeklyCalendar READ homeWeeklyCalendar NOTIFY homeChanged)
    Q_PROPERTY(QVariantList homeTopRated READ homeTopRated NOTIFY homeChanged)
    Q_PROPERTY(QVariantMap homeHeroAnime READ homeHeroAnime NOTIFY homeChanged)
    Q_PROPERTY(bool homeClassifying READ homeClassifying NOTIFY homeChanged)
    Q_PROPERTY(bool loadingDetail READ loadingDetail NOTIFY detailChanged)
    Q_PROPERTY(bool loadingSubject READ loadingSubject NOTIFY detailChanged)
    Q_PROPERTY(bool loadingEpisodes READ loadingEpisodes NOTIFY detailChanged)
    Q_PROPERTY(bool episodesAvailable READ episodesAvailable NOTIFY detailChanged)
    Q_PROPERTY(int episodePage READ episodePage NOTIFY detailChanged)
    Q_PROPERTY(int episodeTotal READ episodeTotal NOTIFY detailChanged)
    Q_PROPERTY(int episodePageCount READ episodePageCount NOTIFY detailChanged)
    Q_PROPERTY(QVariantMap currentDetail READ currentDetail NOTIFY detailChanged)
    Q_PROPERTY(QString detailError READ detailError NOTIFY detailChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)
    Q_PROPERTY(QString searchKeyword READ searchKeyword NOTIFY searchChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchChanged)
    Q_PROPERTY(int searchTotal READ searchTotal NOTIFY searchChanged)
    Q_PROPERTY(bool calendarStale READ calendarStale NOTIFY stateChanged)
    Q_PROPERTY(bool detailStale READ detailStale NOTIFY detailChanged)
    Q_PROPERTY(bool searchStale READ searchStale NOTIFY searchChanged)
    Q_PROPERTY(qint64 jsonCacheBytes READ jsonCacheBytes NOTIFY cacheChanged)
    Q_PROPERTY(qint64 coverCacheBytes READ coverCacheBytes NOTIFY cacheChanged)
    Q_PROPERTY(bool clearingCache READ clearingCache NOTIFY cacheChanged)
    Q_PROPERTY(QString cacheError READ cacheError NOTIFY cacheChanged)
    Q_PROPERTY(bool cacheCleared READ cacheCleared NOTIFY cacheChanged)
    Q_PROPERTY(QObject* coverCache READ coverCache CONSTANT)
public:
    explicit AnimeMetadataService(QString cacheRoot, QObject* parent = nullptr, int timeoutMs = 15000);
    AnimeMetadataService(QString cacheRoot, MetadataCache::Policy policy, QObject* parent = nullptr, int timeoutMs = 15000);
    ~AnimeMetadataService() override;
    bool loading() const { return loading_; }
    QString error() const { return error_; }
    QVariantList airingToday() const { return airingToday_; }
    QVariantList weeklyCalendar() const { return weeklyCalendar_; }
    QVariantList topRated() const { return topRated_; }
    QVariantMap heroAnime() const { return heroAnime_; }
    int todayWeekday() const { return todayWeekday_; }
    QVariantList homeWeeklyCalendar() const { return homeWeeklyCalendar_; }
    QVariantList homeTopRated() const { return homeTopRated_; }
    QVariantMap homeHeroAnime() const { return homeHeroAnime_; }
    bool homeClassifying() const { return !homeQueue_.isEmpty() || !homeActive_.isEmpty(); }
    void setShowChineseAnime(bool show);
    bool loadingDetail() const { return loadingDetail_; }
    bool loadingSubject() const { return detailSubjectLoading_; }
    bool loadingEpisodes() const { return detailEpisodesLoading_; }
    bool episodesAvailable() const { return episodesAvailable_; }
    int episodePage() const { return episodePage_; }
    int episodeTotal() const { return episodeTotal_; }
    int episodePageCount() const { return std::max(1, (episodeTotal_ + 99) / 100); }
    QVariantMap currentDetail() const { return currentDetail_; }
    QString detailError() const { return detailError_; }
    bool searching() const { return searching_; }
    QVariantList searchResults() const { return searchResults_; }
    QString searchKeyword() const { return searchKeyword_; }
    QString searchError() const { return searchError_; }
    int searchTotal() const { return searchTotal_; }
    bool calendarStale() const { return calendarStale_; }
    bool detailStale() const { return detailStale_; }
    bool searchStale() const { return searchStale_; }
    qint64 jsonCacheBytes() const { return jsonBytes_; }
    qint64 coverCacheBytes() const { return coverBytes_; }
    bool clearingCache() const { return clearing_; }
    QString cacheError() const { return cacheError_; }
    bool cacheCleared() const { return cacheCleared_ && jsonBytes_ == 0 && coverBytes_ == 0; }
    QObject* coverCache();
    MetadataCache* cacheStore() { return &cache_; }
    void setBaseUrl(const QString& url);
    QString baseUrl() const { return baseUrl_; }
    Q_INVOKABLE void refreshCalendar(bool force = false);
    Q_INVOKABLE void fetchDetail(qint64 subjectId, bool force = false);
    Q_INVOKABLE void fetchEpisodePage(int page, bool force = false);
    Q_INVOKABLE void searchBangumi(const QString& keyword, bool force = false);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void clearDetail();
    Q_INVOKABLE void clearMetadataCache();
signals:
    void homeChanged();
    void stateChanged();
    void calendarChanged();
    void detailChanged();
    void searchChanged();
    void cacheChanged();
private:
    struct CalendarParsed {
        QVariantList weeklyCalendar, airingToday, topRated;
        QVariantMap heroAnime;
        QString error;
    };
    enum class Kind { Calendar, Subject, Episodes, Search };
    struct Result {
        QVariant value;
        QDateTime fetchedAt;
        bool available = false;
        bool refreshing = false;
        bool stale = false;
        QString error;
    };
    struct Watch { QString scope; std::function<void(Result)> callback; };
    struct Job {
        QString key, identity;
        Kind kind;
        qint64 ttl;
        QList<Watch> watches;
        QPointer<QNetworkReply> reply;
        Result cached;
        bool active = true;
        bool force = false;
    };
    static CalendarParsed parseCalendarJson(const QByteArray&, int weekday, std::stop_token stop = {});
    static QVariantMap parseSubjectDetail(const QByteArray&, const QByteArray& episodes = {}, std::stop_token stop = {});
    static Result parseResource(Kind kind, const QString& identity, const QByteArray& data, int weekday);
    QString resourceKey(Kind kind, const QString& identity) const;
    void request(Kind kind, QString identity, QString scope, bool force, std::function<void(Result)> callback);
    void cached(const std::shared_ptr<Job>& job, MetadataCache::Record record, bool legacy = false);
    void network(const std::shared_ptr<Job>& job);
    void parseNetwork(const std::shared_ptr<Job>& job, QByteArray body);
    void publish(const std::shared_ptr<Job>& job, Result result, bool done);
    void unsubscribe(const QString& scope);
    void cancelAll();
    void applyCalendar(QVariantMap value);
    void fetchHeroDetails(bool force = false);
    void updateDetail();
    void rebuildHome();
    void prepareHome();
    void pumpHome();
    void cancelHome();
    struct HomeInfo { QVariantMap subject; QDateTime fetchedAt; bool available{}; };
    QHash<qint64, HomeInfo> homeInfo_;
    QList<qint64> homeQueue_;
    QSet<qint64> homeActive_;
    QVariantList homeWeeklyCalendar_, homeTopRated_;
    QVariantMap homeHeroAnime_;
    bool homeEnabled_{}, showChineseAnime_{};
    quint64 homeGeneration_{};
    QTimer homePump_;
    MetadataCache cache_;
    CoverCache* covers_{};
    QNetworkAccessManager network_;
    QThreadPool worker_;
    QHash<QString, std::shared_ptr<Job>> jobs_;
    QHash<QString, QDateTime> failedAt_;
    QTimer refreshTimer_;
    QString baseUrl_{QStringLiteral("https://api.bgm.tv")};
    int timeoutMs_;
    QDate calendarDate_;
    QDateTime calendarFetchedAt_, heroFetchedAt_;
    bool loading_{}, loadingDetail_{}, searching_{}, calendarStale_{}, detailStale_{}, searchStale_{}, clearing_{}, cacheCleared_{};
    bool detailSubjectLoading_{}, detailEpisodesLoading_{}, detailSubjectStale_{}, detailEpisodesStale_{};
    bool episodesAvailable_{};
    int episodePage_{}, episodeTotal_{};
    QDateTime episodeTotalFetchedAt_;
    quint64 episodeGeneration_{};
    QString error_, detailError_, searchError_, searchKeyword_, cacheError_, subjectError_, episodesError_;
    QVariantList weeklyCalendar_, airingToday_, topRated_, searchResults_, detailEpisodes_;
    QVariantMap heroAnime_, currentDetail_, detailSubject_;
    int todayWeekday_{1}, searchTotal_{};
    qint64 detailId_{}, jsonBytes_{}, coverBytes_{};
    quint64 calendarGeneration_{}, detailGeneration_{}, searchGeneration_{}, heroGeneration_{};
};
