#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>
#include <QTimer>
#include <QDateTime>
#include <QJsonArray>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <QDir>
#include <QThreadPool>
#include <memory>
#include <stop_token>

class AnimeMetadataService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QVariantList airingToday READ airingToday NOTIFY calendarChanged)
    Q_PROPERTY(QVariantList weeklyCalendar READ weeklyCalendar NOTIFY calendarChanged)
    Q_PROPERTY(QVariantList topRated READ topRated NOTIFY calendarChanged)
    Q_PROPERTY(QVariantMap heroAnime READ heroAnime NOTIFY calendarChanged)
    Q_PROPERTY(int todayWeekday READ todayWeekday NOTIFY calendarChanged)
    Q_PROPERTY(bool loadingDetail READ loadingDetail NOTIFY detailChanged)
    Q_PROPERTY(QVariantMap currentDetail READ currentDetail NOTIFY detailChanged)
    Q_PROPERTY(QString detailError READ detailError NOTIFY detailChanged)
    Q_PROPERTY(bool searching READ searching NOTIFY searchChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchChanged)
    Q_PROPERTY(QString searchKeyword READ searchKeyword NOTIFY searchChanged)
    Q_PROPERTY(QString searchError READ searchError NOTIFY searchChanged)
    Q_PROPERTY(int searchTotal READ searchTotal NOTIFY searchChanged)

public:
    explicit AnimeMetadataService(QString cacheRoot, QObject* parent = nullptr, int timeoutMs = 15000);
    ~AnimeMetadataService() override;

    bool loading() const { return loading_; }
    QString error() const { return error_; }
    QVariantList airingToday() const { return airingToday_; }
    QVariantList weeklyCalendar() const { return weeklyCalendar_; }
    QVariantList topRated() const { return topRated_; }
    QVariantMap heroAnime() const { return heroAnime_; }
    int todayWeekday() const { return todayWeekday_; }

    bool loadingDetail() const { return loadingDetail_; }
    QVariantMap currentDetail() const { return currentDetail_; }
    QString detailError() const { return detailError_; }

    bool searching() const { return searching_; }
    QVariantList searchResults() const { return searchResults_; }
    QString searchKeyword() const { return searchKeyword_; }
    QString searchError() const { return searchError_; }
    int searchTotal() const { return searchTotal_; }

    // Custom base URL for testing / mock servers (defaults to https://api.bgm.tv)
    void setBaseUrl(const QString& url);
    QString baseUrl() const { return baseUrl_; }

    Q_INVOKABLE void refreshCalendar(bool force = false);
    Q_INVOKABLE void fetchDetail(qint64 subjectId);
    Q_INVOKABLE void searchBangumi(const QString& keyword);
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void clearDetail();

signals:
    void stateChanged();
    void calendarChanged();
    void detailChanged();
    void searchChanged();

private:
    struct CalendarParsed {
        QVariantList weeklyCalendar;
        QVariantList airingToday;
        QVariantList topRated;
        QVariantMap heroAnime;
        QString error;
    };

    void loadCalendarFromDiskCache();
    void saveCalendarToDiskCache(const QByteArray& data);
    static CalendarParsed parseCalendarJson(const QByteArray& json, int todayDayOfWeek, std::stop_token stop = {});
    static QVariantMap parseSubjectDetail(const QByteArray& json, const QByteArray& episodesJson = {}, std::stop_token stop = {});
    void fetchHeroDetails();
    void fetchEpisodes(qint64 subjectId, quint64 generation, QByteArray subject, QJsonArray episodes = {});

    QString cacheDir_;
    QString baseUrl_{QStringLiteral("https://api.bgm.tv")};
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> calendarReply_;
    QPointer<QNetworkReply> detailReply_;
    QPointer<QNetworkReply> epReply_;
    QPointer<QNetworkReply> searchReply_;
    QPointer<QNetworkReply> heroReply_;
    QTimer deadline_;
    QTimer refreshTimer_;
    QDateTime calendarFetchedAt_;
    int timeoutMs_;

    bool loading_{false};
    bool loadingDetail_{false};
    bool searching_{false};
    QString error_;
    QString detailError_;
    int todayWeekday_{1};

    QVariantList weeklyCalendar_;
    QVariantList airingToday_;
    QVariantList topRated_;
    QVariantMap heroAnime_;
    QVariantMap currentDetail_;
    QVariantList searchResults_;
    QString searchKeyword_, searchError_;
    int searchTotal_{};
    quint64 searchGeneration_{};
    std::stop_source searchStop_;

    QThreadPool worker_;
    std::stop_source workerStop_;
    quint64 generation_{0};
    quint64 detailGeneration_{0};
};
