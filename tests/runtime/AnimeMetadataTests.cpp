#include "infrastructure/AnimeMetadataService.h"
#include "infrastructure/CoverCache.h"
#include <QBuffer>
#include <QImage>
#include <QTimeZone>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <QUrlQuery>

namespace {
QByteArray mockCalendarJson() {
    QJsonArray root;
    for (int day = 1; day <= 7; ++day) {
        QJsonObject dayObj;
        QJsonObject weekdayObj;
        weekdayObj[QStringLiteral("id")] = day;
        weekdayObj[QStringLiteral("cn")] = QStringLiteral("周") + QString::number(day);
        weekdayObj[QStringLiteral("en")] = QStringLiteral("Day") + QString::number(day);
        dayObj[QStringLiteral("weekday")] = weekdayObj;

        QJsonArray items;
        for (int i = 1; i <= 3; ++i) {
            QJsonObject item;
            const qint64 id = day * 100 + i;
            item[QStringLiteral("id")] = id;
            item[QStringLiteral("name")] = QStringLiteral("Original Anime ") + QString::number(id);
            item[QStringLiteral("name_cn")] = QStringLiteral("测试番剧 ") + QString::number(id);
            item[QStringLiteral("air_date")] = QStringLiteral("2026-10-0") + QString::number(day);

            QJsonObject rating;
            rating[QStringLiteral("total")] = 100 * i;
            rating[QStringLiteral("score")] = 7.0 + (day * 0.3) + (i * 0.1);
            item[QStringLiteral("rating")] = rating;

            QJsonObject images;
            images[QStringLiteral("large")] = QStringLiteral("http://lain.bgm.tv/pic/cover/l/test_") + QString::number(id) + QStringLiteral(".jpg");
            images[QStringLiteral("common")] = QStringLiteral("http://lain.bgm.tv/pic/cover/c/test_") + QString::number(id) + QStringLiteral(".jpg");
            item[QStringLiteral("images")] = images;

            items.append(item);
        }
        dayObj[QStringLiteral("items")] = items;
        root.append(dayObj);
    }
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray mockSubjectJson(qint64 id) {
    QJsonObject obj;
    obj[QStringLiteral("id")] = id;
    obj[QStringLiteral("name")] = QStringLiteral("Original Subject");
    obj[QStringLiteral("name_cn")] = QStringLiteral("测试番剧详情");
    obj[QStringLiteral("summary")] = QStringLiteral("这是一段精彩的剧情简介内容。");
    obj[QStringLiteral("date")] = QStringLiteral("2026-10-01");
    obj[QStringLiteral("platform")] = QStringLiteral("TV");
    obj[QStringLiteral("eps")] = 12;

    QJsonObject rating;
    rating[QStringLiteral("score")] = 8.8;
    rating[QStringLiteral("total")] = 2500;
    obj[QStringLiteral("rating")] = rating;

    QJsonObject images;
    images[QStringLiteral("large")] = QStringLiteral("https://lain.bgm.tv/pic/cover/l/detail.jpg");
    obj[QStringLiteral("images")] = images;

    QJsonArray tags;
    QJsonObject tag1; tag1[QStringLiteral("name")] = QStringLiteral("奇幻"); tags.append(tag1);
    QJsonObject tag2; tag2[QStringLiteral("name")] = QStringLiteral("冒险"); tags.append(tag2);
    obj[QStringLiteral("tags")] = tags;

    return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

QByteArray mockEpisodesJson(qint64 subjectId, int count = 4, int offset = 0) {
    QJsonObject root;
    QJsonArray array;
    for (int i = offset + 1; i <= std::min(count, offset + 100); ++i) {
        QJsonObject ep;
        ep[QStringLiteral("id")] = subjectId * 1000 + i;
        ep[QStringLiteral("sort")] = i;
        ep[QStringLiteral("name")] = QStringLiteral("Episode ") + QString::number(i);
        ep[QStringLiteral("name_cn")] = QStringLiteral("第 %1 话 冒险旅程").arg(i);
        ep[QStringLiteral("airdate")] = QStringLiteral("2026-10-0") + QString::number(i);
        ep[QStringLiteral("type")] = 0;
        array.append(ep);
    }
    root[QStringLiteral("data")] = array;
    root[QStringLiteral("total")] = count;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

class MockBangumiServer final : public QTcpServer {
public:
    int status{200};
    int detailDelay{}, episodeDelay{}, episodeStatus{200}, episodeCount{4};
    int calendarRequests{}, subjectRequests{}, episodeRequests{};
    int searchRequests{}, searchDelay{}, searchStatus{200};
    int coverRequests{}, coverDelay{}, coverStatus{200}, failEpisodeOffset{-1};
    bool emptySummary{};
    QJsonObject searchPayload;
    QByteArray searchTarget, searchMethod, searchBody;
    QByteArray calendarBody, coverBody;
    explicit MockBangumiServer() {
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto request = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, request] {
                    request->append(socket->readAll());
                    if (!request->contains("\r\n\r\n")) return;
                    const auto headerEnd = request->indexOf("\r\n\r\n") + 4;
                    const auto length = QRegularExpression("(?im)^Content-Length:\\s*(\\d+)")
                        .match(QString::fromLatin1(request->left(headerEnd))).captured(1).toLongLong();
                    if (request->size() < headerEnd + length) return;
                    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
                    const auto target = request->split(' ').value(1);

                    if (target.startsWith("/calendar")) ++calendarRequests;
                    else if (target.startsWith("/v0/search/subjects")) ++searchRequests;
                    else if (target.startsWith("/v0/subjects/")) ++subjectRequests;
                    else if (target.startsWith("/v0/episodes")) ++episodeRequests;
                    else if (target.startsWith("/cover")) ++coverRequests;

                    QByteArray body;
                    int responseStatus = status;
                    int delay = 0;
                    if (status != 200) {
                        body = "Internal Server Error";
                    } else if (target.startsWith("/v0/search/subjects")) {
                        searchMethod = request->split(' ').value(0);
                        searchTarget = target;
                        searchPayload = QJsonDocument::fromJson(request->mid(headerEnd)).object();
                        auto item = QJsonDocument::fromJson(mockSubjectJson(searchPayload.value("keyword").toString() == "new" ? 202 : 101)).object();
                        item["type"] = 2;
                        body = searchBody.isEmpty() ? QJsonDocument(QJsonObject{{"total", 45}, {"data", QJsonArray{item}}}).toJson() : searchBody;
                        delay = searchDelay;
                        responseStatus = searchStatus;
                    } else if (target.startsWith("/calendar")) {
                        body = calendarBody.isEmpty() ? mockCalendarJson() : calendarBody;
                    } else if (target.startsWith("/v0/subjects/")) {
                        body = mockSubjectJson(target.mid(QByteArray("/v0/subjects/").size()).toLongLong());
                        if (emptySummary) { auto obj = QJsonDocument::fromJson(body).object(); obj["summary"] = ""; body = QJsonDocument(obj).toJson(); }
                        delay = detailDelay;
                    } else if (target.startsWith("/v0/episodes")) {
                        const QUrlQuery query(QUrl(QStringLiteral("http://localhost") + QString::fromUtf8(target)));
                        body = mockEpisodesJson(query.queryItemValue("subject_id").toLongLong(), episodeCount, query.queryItemValue("offset").toInt());
                        delay = episodeDelay;
                        responseStatus = episodeStatus;
                        if (query.queryItemValue("offset").toInt() == failEpisodeOffset) responseStatus = 503;
                    } else if (target.startsWith("/cover")) {
                        body = coverBody; delay = coverDelay; responseStatus = coverStatus;
                    }

                    QTimer::singleShot(delay, this, [guard = QPointer<QTcpSocket>(socket), body, responseStatus] {
                    if (!guard || guard->state() != QAbstractSocket::ConnectedState) return;
                    guard->write("HTTP/1.1 " + QByteArray::number(responseStatus) + " OK\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                                  "Connection: close\r\n\r\n" + body);
                    guard->disconnectFromHost();
                    });
                });
            }
        });
        listen(QHostAddress::LocalHost, 0);
    }

    QString url() const {
        return QStringLiteral("http://127.0.0.1:%1").arg(serverPort());
    }
};
} // namespace

namespace {
class TestClock {
public:
    TestClock(QDateTime value) : stamp_(value.toMSecsSinceEpoch()) {}
    TestClock& operator=(const QDateTime& value) { stamp_ = value.toMSecsSinceEpoch(); return *this; }
    QDateTime value() const { return QDateTime::fromMSecsSinceEpoch(stamp_.load(), QTimeZone::UTC); }
    operator QDateTime() const { return value(); }
    QDateTime addSecs(qint64 seconds) const { return value().addSecs(seconds); }
    QDateTime addDays(qint64 days) const { return value().addDays(days); }
    QDateTime toLocalTime() const { return value().toLocalTime(); }
private:
    std::atomic<qint64> stamp_;
};
MetadataCache::Record readRecord(MetadataCache* store, const QString& key, bool cover = false) {
    struct Pending { bool done = false; MetadataCache::Record value; };
    auto pending = std::make_shared<Pending>();
    store->read(key, cover, [pending](MetadataCache::Record value) { pending->value = std::move(value); pending->done = true; });
    for (int i = 0; i < 500 && !pending->done; ++i) QTest::qWait(10);
    return pending->value;
}
QByteArray imageBytes(QColor color = Qt::red) {
    QImage image(24, 32, QImage::Format_ARGB32); image.fill(color);
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "PNG"); return bytes;
}
}

class AnimeMetadataTests : public QObject {
    Q_OBJECT
private slots:
    void coverLoadingStates() {
        MockBangumiServer server; QTemporaryDir dir;
        server.coverBody = imageBytes(); server.coverDelay = 180;
        AnimeMetadataService service(dir.path());
        auto* covers = qobject_cast<CoverCache*>(service.coverCache()); QObject owner;
        auto* request = qobject_cast<CoverRequest*>(covers->createRequest(&owner));
        QVERIFY(!request->loading());
        request->setUrl(QUrl(server.url() + "/cover")); QVERIFY(request->loading());
        QTRY_VERIFY(!request->loading()); QVERIFY(!request->source().isEmpty());
        request->setUrl(QUrl(server.url() + "/cover-failure")); QVERIFY(request->loading());
        server.coverStatus = 503;
        QTRY_VERIFY(!request->loading()); QVERIFY(request->source().isEmpty());
        const auto count = server.coverRequests;
        request->retry(); QTRY_VERIFY(!request->loading()); QCOMPARE(server.coverRequests, count);
        request->setUrl(QUrl()); QVERIFY(!request->loading());
        request->setUrl(QUrl("file:///invalid.png")); QVERIFY(!request->loading());
        request->setUrl(QUrl(server.url() + "/cover-reset")); QVERIFY(request->loading());
        covers->reset(); QVERIFY(!request->loading());
        QTest::qWait(220); QVERIFY(request->source().isEmpty());
    }
    void independentDetailLoadingStates() {
        QTemporaryDir dir;
        MockBangumiServer server;
        server.episodeDelay = 350;
        AnimeMetadataService service(dir.path()); service.setBaseUrl(server.url());
        service.fetchDetail(101);
        QVERIFY(service.loadingSubject()); QVERIFY(service.loadingEpisodes()); QVERIFY(!service.episodesAvailable());
        QTRY_VERIFY(!service.loadingSubject());
        QCOMPARE(service.currentDetail().value("id").toLongLong(), 101);
        QVERIFY(service.loadingEpisodes()); QVERIFY(!service.episodesAvailable());
        QTRY_VERIFY(!service.loadingDetail()); QVERIFY(service.episodesAvailable());
        QCOMPARE(service.currentDetail().value("episodes").toList().size(), 4);
        service.fetchDetail(101, true);
        QVERIFY(service.episodesAvailable());
        QCOMPARE(service.currentDetail().value("episodes").toList().size(), 4);
        QTRY_VERIFY(!service.loadingDetail());
        server.episodeCount = 0;
        service.fetchDetail(202);
        QVERIFY(!service.episodesAvailable());
        QTRY_VERIFY(!service.loadingDetail()); QVERIFY(service.episodesAvailable());
        QVERIFY(service.currentDetail().value("episodes").toList().isEmpty());
        server.episodeStatus = 503;
        service.fetchDetail(303);
        QTRY_VERIFY(!service.loadingDetail()); QVERIFY(!service.episodesAvailable());
        QVERIFY(!service.detailError().isEmpty());
        service.clearDetail();
        QVERIFY(!service.loadingSubject()); QVERIFY(!service.loadingEpisodes()); QVERIFY(!service.episodesAvailable());
    }
    void calendarReentryRetainsSnapshotAndRefreshBoundaries() {
        MockBangumiServer server; QTemporaryDir dir;
        TestClock now(QDateTime(QDate(2026, 10, 15), QTime(10, 0), QTimeZone::systemTimeZone()).toUTC());
        MetadataCache::Policy policy; policy.now = [&] { return now.value(); };
        AnimeMetadataService service(dir.path(), policy); service.setBaseUrl(server.url());
        service.refreshCalendar();
        QTRY_VERIFY(!service.loading() && service.heroAnime().contains("summary"));
        const auto calendar = service.weeklyCalendar();
        const auto hero = service.heroAnime();
        const auto subjects = server.subjectRequests;
        QSignalSpy models(&service, &AnimeMetadataService::calendarChanged);
        QSignalSpy states(&service, &AnimeMetadataService::stateChanged);
        for (int visit = 0; visit < 4; ++visit) { service.refreshCalendar(); QVERIFY(!service.loading()); }
        QTest::qWait(60);
        QCOMPARE(models.count(), 0); QCOMPARE(states.count(), 0);
        QCOMPARE(server.calendarRequests, 1); QCOMPARE(server.subjectRequests, subjects);
        QCOMPARE(service.weeklyCalendar(), calendar); QCOMPARE(service.heroAnime(), hero);

        now = now.addSecs(21600); // Expiry at exactly six hours must still refresh.
        server.status = 503; service.refreshCalendar();
        QCOMPARE(service.weeklyCalendar(), calendar);
        QTRY_VERIFY(!service.loading());
        QCOMPARE(server.calendarRequests, 2); QVERIFY(service.calendarStale()); QVERIFY(!service.error().isEmpty());
        QCOMPARE(models.count(), 0); // Identical stale content must not rebuild the page.
        states.clear(); service.refreshCalendar(); QVERIFY(!service.loading());
        QCOMPARE(states.count(), 0);
        QCOMPARE(server.calendarRequests, 2); // Retain the failure cooldown.
        server.status = 200; service.refreshCalendar(true); QTRY_VERIFY(!service.loading());
        QCOMPARE(server.calendarRequests, 3); QVERIFY(!service.calendarStale()); QVERIFY(service.error().isEmpty());
        QTRY_COMPARE(server.subjectRequests, subjects + 1); // Manual refresh also updates the hero.
        QTRY_VERIFY(service.heroAnime().contains("summary"));
        QCOMPARE(service.weeklyCalendar(), calendar); QCOMPARE(models.count(), 0);

        service.clearMetadataCache(); QTRY_VERIFY(!service.clearingCache());
        QVERIFY(service.weeklyCalendar().isEmpty());
        service.refreshCalendar(); QTRY_VERIFY(!service.loading());
        QCOMPARE(server.calendarRequests, 4); QCOMPARE(service.weeklyCalendar(), calendar);
        MockBangumiServer other;
        service.setBaseUrl(other.url()); QVERIFY(service.weeklyCalendar().isEmpty());
        service.refreshCalendar(); QTRY_VERIFY(!service.loading());
        QCOMPARE(other.calendarRequests, 1);
    }
    void freshCalendarRollsOverDateAndRejectsFutureTimestamp() {
        MockBangumiServer server; QTemporaryDir dir;
        TestClock now(QDateTime(QDate(2026, 10, 15), QTime(23, 30), QTimeZone::systemTimeZone()).toUTC());
        MetadataCache::Policy policy; policy.now = [&] { return now.value(); };
        AnimeMetadataService service(dir.path(), policy); service.setBaseUrl(server.url()); service.refreshCalendar();
        QTRY_VERIFY(!service.loading() && service.heroAnime().contains("summary"));
        const auto previousHero = service.heroAnime().value("id");
        now = now.addSecs(3600); service.refreshCalendar();
        QTRY_VERIFY(!service.loading() && service.heroAnime().contains("summary"));
        QCOMPARE(service.todayWeekday(), now.toLocalTime().date().dayOfWeek());
        QVERIFY(service.heroAnime().value("id") != previousHero);
        QCOMPARE(server.calendarRequests, 1); // Derive the new day from the still-fresh disk snapshot.
        QCOMPARE(server.subjectRequests, 2);
        now = now.addSecs(-3601); service.refreshCalendar(); QTRY_VERIFY(!service.loading());
        QCOMPARE(server.calendarRequests, 2); // A future fetchedAt cannot suppress refresh.
    }
    void restartOfflineAndIndependentExpiry() {
        MockBangumiServer server;
        QTemporaryDir dir;
        TestClock now(QDateTime::fromString("2026-10-15T08:00:00.000Z", Qt::ISODateWithMs));
        MetadataCache::Policy policy; policy.now = [&] { return now.value(); };
        {
            AnimeMetadataService service(dir.path(), policy); service.setBaseUrl(server.url());
            service.fetchDetail(101); service.searchBangumi("cached"); service.refreshCalendar();
            QTRY_VERIFY(!service.loadingDetail() && !service.searching() && !service.loading());
            QTRY_VERIFY(service.heroAnime().contains("summary"));
            QVERIFY(readRecord(service.cacheStore(), server.url() + "|2|101").valid);
        }
        const int subjects = server.subjectRequests, episodes = server.episodeRequests, searches = server.searchRequests, calendars = server.calendarRequests;
        server.status = 503;
        AnimeMetadataService service(dir.path(), policy); service.setBaseUrl(server.url());
        service.fetchDetail(101); service.searchBangumi("cached"); service.refreshCalendar();
        QTRY_VERIFY(!service.loadingDetail() && !service.searching() && !service.loading());
        QCOMPARE(service.currentDetail().value("episodes").toList().size(), 4);
        QCOMPARE(service.searchResults().size(), 1); QCOMPARE(service.weeklyCalendar().size(), 7);
        QCOMPARE(server.subjectRequests, subjects); QCOMPARE(server.episodeRequests, episodes);
        QCOMPARE(server.searchRequests, searches); QCOMPARE(server.calendarRequests, calendars);
        now = now.addSecs(21600); // Episodes expire exactly at six hours; subject remains fresh.
        service.fetchDetail(101);
        QTRY_VERIFY(!service.loadingDetail());
        QVERIFY(service.detailStale()); QVERIFY(!service.detailError().isEmpty());
        QCOMPARE(service.currentDetail().value("episodes").toList().size(), 4);
        QCOMPARE(server.subjectRequests, subjects); QCOMPARE(server.episodeRequests, episodes + 1);
        service.fetchDetail(101); QTRY_VERIFY(!service.loadingDetail());
        QCOMPARE(server.episodeRequests, episodes + 1); // Failure cooldown.
        server.status = 200;
        service.fetchDetail(101, true); QTRY_VERIFY(!service.loadingDetail());
        QVERIFY(service.detailError().isEmpty()); QVERIFY(!service.detailStale());
        now = now.addSecs(86400);
        server.status = 503;
        service.fetchDetail(101); QTRY_VERIFY(!service.loadingDetail());
        QCOMPARE(server.subjectRequests, subjects + 2);
        QVERIFY(!service.currentDetail().isEmpty());
    }
    void completeEpisodesSurviveLaterPageFailure() {
        MockBangumiServer server; QTemporaryDir dir;
        server.episodeCount = 205;
        AnimeMetadataService service(dir.path()); service.setBaseUrl(server.url()); service.fetchDetail(101);
        QTRY_VERIFY(!service.loadingDetail());
        const auto key = server.url() + "|2|101";
        const auto original = readRecord(service.cacheStore(), key);
        QVERIFY(original.valid);
        server.episodeCount = 250; server.failEpisodeOffset = 100;
        service.fetchDetail(101, true); QTRY_VERIFY(!service.loadingDetail());
        QVERIFY(!service.detailError().isEmpty());
        QCOMPARE(service.currentDetail().value("episodes").toList().size(), 205);
        QCOMPARE(readRecord(service.cacheStore(), key).data, original.data);
        service.fetchDetail(202, true); QTRY_VERIFY(!service.loadingDetail());
        QCOMPARE(service.currentDetail().value("episodes").toList().size(), 100);
        QVERIFY(!readRecord(service.cacheStore(), server.url() + "|2|202").valid);
    }
    void sharedHeroSubjectAndEmptySummary() {
        MockBangumiServer server; QTemporaryDir dir; server.emptySummary = true; server.detailDelay = 100;
        AnimeMetadataService service(dir.path()); service.setBaseUrl(server.url()); service.refreshCalendar();
        QTRY_VERIFY(!service.weeklyCalendar().isEmpty());
        const auto id = service.heroAnime().value("id").toLongLong();
        QTRY_COMPARE(server.subjectRequests, 1);
        service.refreshCalendar(); QTRY_VERIFY(!service.loading());
        service.fetchDetail(id); QTRY_VERIFY(!service.loadingDetail());
        QCOMPARE(server.subjectRequests, 1);
        QVERIFY(service.heroAnime().contains("summary"));
        service.refreshCalendar(); QTRY_VERIFY(!service.loading()); QTest::qWait(160);
        QCOMPARE(server.subjectRequests, 1);
    }
    void searchEmptyExpiryAndSourceIsolation() {
        MockBangumiServer server; QTemporaryDir dir;
        TestClock now(QDateTime::currentDateTimeUtc()); MetadataCache::Policy policy; policy.now = [&] { return now.value(); };
        server.searchBody = R"({"data":[],"total":0})";
        {
            AnimeMetadataService service(dir.path(), policy); service.setBaseUrl(server.url()); service.searchBangumi("empty");
            QTRY_VERIFY(!service.searching()); QVERIFY(service.searchError().isEmpty());
        }
        AnimeMetadataService service(dir.path(), policy); service.setBaseUrl(server.url()); service.searchBangumi("empty");
        QTRY_VERIFY(!service.searching()); QCOMPARE(server.searchRequests, 1);
        now = now.addSecs(3600); server.searchStatus = 503;
        service.searchBangumi("empty"); QTRY_VERIFY(!service.searching());
        QCOMPARE(server.searchRequests, 2); QVERIFY(service.searchStale()); QVERIFY(!service.searchError().isEmpty());
        service.setBaseUrl("http://127.0.0.1:1"); service.searchBangumi("empty"); QTRY_VERIFY(!service.searching());
        QVERIFY(!service.searchStale()); QVERIFY(!service.searchError().isEmpty());
    }
    void clearCancelsLateWritesAndPreservesOtherCaches() {
        MockBangumiServer server; QTemporaryDir dir;
        QDir().mkpath(dir.filePath("danmaku")); QFile other(dir.filePath("danmaku/keep.json"));
        QVERIFY(other.open(QIODevice::WriteOnly)); other.write("keep"); other.close();
        server.detailDelay = server.searchDelay = server.episodeDelay = 150;
        AnimeMetadataService service(dir.path()); service.setBaseUrl(server.url());
        service.fetchDetail(101); service.searchBangumi("late");
        QTRY_COMPARE(server.subjectRequests, 1);
        service.clearMetadataCache(); QTRY_VERIFY(!service.clearingCache()); QTest::qWait(200);
        QVERIFY(service.currentDetail().isEmpty()); QVERIFY(service.searchResults().isEmpty());
        QVERIFY(!readRecord(service.cacheStore(), server.url() + "|1|101").valid);
        QVERIFY(other.exists()); QCOMPARE(service.jsonCacheBytes(), 0);
        server.detailDelay = server.episodeDelay = 0;
        service.fetchDetail(101); QTRY_VERIFY(!service.loadingDetail()); QVERIFY(!service.currentDetail().isEmpty());
    }
    void calendarMigrationCrossDateAndFutureClock() {
        QTemporaryDir dir; QDir().mkpath(dir.filePath("metadata"));
        QFile old(dir.filePath("metadata/calendar.json")); QVERIFY(old.open(QIODevice::WriteOnly)); old.write(mockCalendarJson()); old.close();
        TestClock now(QFileInfo(old).lastModified().toUTC());
        MetadataCache::Policy policy; policy.now = [&] { return now.value(); };
        AnimeMetadataService service(dir.path(), policy);
        for (int day = 1; day <= 7; ++day) service.cacheStore()->write("https://api.bgm.tv|1|" + QString::number(day * 100 + 3), false, mockSubjectJson(day * 100 + 3), now);
        service.refreshCalendar();
        QTRY_VERIFY(!service.loading()); QCOMPARE(service.weeklyCalendar().size(), 7);
        QTRY_VERIFY(!old.exists());
        const auto key = "https://api.bgm.tv|0|";
        QVERIFY(readRecord(service.cacheStore(), key).valid);
        QVERIFY(old.open(QIODevice::WriteOnly)); old.write(mockCalendarJson()); old.close();
        service.refreshCalendar(); QTRY_VERIFY(!service.loading()); QTRY_VERIFY(!old.exists());
        // Move from a date's 23:30 to the next day's 00:30, retaining a fresh calendar.
        now = QDateTime(now.toLocalTime().date(), QTime(23, 30), QTimeZone::systemTimeZone()).toUTC();
        service.cacheStore()->write(key, false, mockCalendarJson(), now);
        for (int day = 1; day <= 7; ++day) service.cacheStore()->write("https://api.bgm.tv|1|" + QString::number(day * 100 + 3), false, mockSubjectJson(day * 100 + 3), now);
        QVERIFY(readRecord(service.cacheStore(), key).valid);
        service.refreshCalendar(); QTRY_VERIFY(!service.loading());
        now = now.addSecs(3600); service.refreshCalendar(); QTRY_VERIFY(!service.loading());
        QCOMPARE(service.todayWeekday(), now.toLocalTime().date().dayOfWeek());
        const auto record = readRecord(service.cacheStore(), key);
        now = record.fetchedAt.addMSecs(-1); QVERIFY(!service.cacheStore()->fresh(record, 21600));
        service.clearMetadataCache(); QTRY_VERIFY(!service.clearingCache()); QVERIFY(!old.exists());
    }
    void coverPersistenceDeduplicationAndFailure() {
        MockBangumiServer server; QTemporaryDir dir; server.coverBody = imageBytes();
        TestClock now(QDateTime::currentDateTimeUtc()); MetadataCache::Policy policy; policy.now = [&] { return now.value(); };
        QUrl first;
        {
            AnimeMetadataService service(dir.path(), policy);
            auto* covers = qobject_cast<CoverCache*>(service.coverCache()); QObject owner;
            auto* a = qobject_cast<CoverRequest*>(covers->createRequest(&owner)); auto* b = qobject_cast<CoverRequest*>(covers->createRequest(&owner));
            a->setUrl(QUrl(server.url() + "/cover")); b->setUrl(a->url());
            QTRY_VERIFY(!a->source().isEmpty() && !b->source().isEmpty());
            QCOMPARE(server.coverRequests, 1); first = a->source(); QCOMPARE(first, b->source()); QVERIFY(first.isLocalFile());
        }
        AnimeMetadataService service(dir.path(), policy);
        auto* covers = qobject_cast<CoverCache*>(service.coverCache()); QObject owner;
        auto* a = qobject_cast<CoverRequest*>(covers->createRequest(&owner)); a->setUrl(QUrl(server.url() + "/cover"));
        QTRY_VERIFY(!a->source().isEmpty()); QCOMPARE(server.coverRequests, 1); QCOMPARE(a->source(), first);
        QSignalSpy changes(a, &CoverRequest::sourceChanged);
        a->retry(); QTest::qWait(40); QCOMPARE(server.coverRequests, 1); QCOMPARE(changes.count(), 0);
        now = now.addDays(30); server.coverStatus = 503;
        a->retry(); QCOMPARE(a->source(), first); QTRY_COMPARE(server.coverRequests, 2);
        auto* b = qobject_cast<CoverRequest*>(covers->createRequest(&owner)); b->setUrl(a->url());
        QTRY_COMPARE(server.coverRequests, 2); QTest::qWait(80); QCOMPARE(b->source(), first);
        QCOMPARE(changes.count(), 0); // Expiry/failure preserves the already displayed image.
        now = now.addSecs(301); server.coverStatus = 200; server.coverBody = imageBytes(Qt::blue);
        auto* c = qobject_cast<CoverRequest*>(covers->createRequest(&owner)); c->setUrl(a->url());
        QTRY_VERIFY(!c->source().isEmpty() && c->source() != first); QCOMPARE(a->source(), c->source());
        QTRY_VERIFY(!QFileInfo::exists(first.toLocalFile()));
        server.coverDelay = 150;
        auto* late = qobject_cast<CoverRequest*>(covers->createRequest(&owner)); late->setUrl(QUrl(server.url() + "/cover-late"));
        QTRY_COMPARE(server.coverRequests, 4);
        service.clearMetadataCache(); QTRY_VERIFY(!service.clearingCache()); QTest::qWait(180);
        QVERIFY(a->source().isEmpty() && late->source().isEmpty()); QCOMPARE(service.coverCacheBytes(), 0);
    }
    void coverRejectsInvalidAndOversized_data() {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("invalid") << QByteArray("not an image");
        QTest::newRow("transfer-limit") << QByteArray(8 * 1024 * 1024 + 1, 'x');
        // BMP header advertises an excessive dimension before any pixel allocation.
        QImage image(8193, 1, QImage::Format_RGB32); image.fill(Qt::white);
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly); image.save(&buffer, "BMP");
        QTest::newRow("dimension-limit") << bytes;
        QImage pixels(4001, 4000, QImage::Format_Mono); pixels.fill(0);
        bytes.clear(); QBuffer second(&bytes); second.open(QIODevice::WriteOnly); pixels.save(&second, "PNG");
        QTest::newRow("pixel-limit") << bytes;
    }
    void coverConcurrencyAndSessionOnlyBudget() {
        MockBangumiServer server; QTemporaryDir dir; server.coverBody = imageBytes(); server.coverDelay = 200;
        MetadataCache::Policy policy; policy.coverBudget = 0;
        AnimeMetadataService service(dir.path(), policy);
        auto* covers = qobject_cast<CoverCache*>(service.coverCache()); QObject owner;
        QList<CoverRequest*> handles;
        for (int i = 0; i < 8; ++i) {
            auto* handle = qobject_cast<CoverRequest*>(covers->createRequest(&owner));
            handle->setUrl(QUrl(server.url() + "/cover/" + QString::number(i))); handles.append(handle);
        }
        QTRY_COMPARE(server.coverRequests, 4); QTest::qWait(40); QCOMPARE(server.coverRequests, 4);
        QTRY_VERIFY(std::all_of(handles.begin(), handles.end(), [](const auto* handle) { return !handle->source().isEmpty(); }));
        QCOMPARE(server.coverRequests, 8);
        for (const auto* handle : handles) {
            QCOMPARE(handle->source().scheme(), QString("data"));
            QVERIFY(!readRecord(service.cacheStore(), "cover|" + handle->url().toString(QUrl::FullyEncoded), true).valid);
        }
        QCOMPARE(service.coverCacheBytes(), 0);
    }
    void coverRejectsInvalidAndOversized() {
        QFETCH(QByteArray, bytes); MockBangumiServer server; QTemporaryDir dir; server.coverBody = bytes;
        AnimeMetadataService service(dir.path()); auto* covers = qobject_cast<CoverCache*>(service.coverCache()); QObject owner;
        auto* request = qobject_cast<CoverRequest*>(covers->createRequest(&owner)); request->setUrl(QUrl(server.url() + "/cover"));
        QTRY_COMPARE(server.coverRequests, 1); QTest::qWait(200);
        QVERIFY(request->source().isEmpty());
        QVERIFY(!readRecord(service.cacheStore(), "cover|" + request->url().toString(QUrl::FullyEncoded), true).valid);
    }
    void storeBudgetCorruptionAndClearBarrier() {
        QTemporaryDir dir; TestClock now(QDateTime::currentDateTimeUtc());
        MetadataCache::Policy policy; policy.jsonBudget = 480; policy.coverBudget = 500; policy.now = [&] { return now.value(); };
        MetadataCache store(dir.path(), policy);
        const QByteArray payload = QJsonDocument(QJsonObject{{"text", QString(120, 'x')}}).toJson(QJsonDocument::Compact);
        store.write("old", false, payload, now); QVERIFY(readRecord(&store, "old").valid);
        now = now.addSecs(1); store.setPins("test", {"old"});
        store.write("new", false, payload, now); QVERIFY(!readRecord(&store, "new").valid); QVERIFY(readRecord(&store, "old").valid);
        store.setPins("test", {}); now = now.addSecs(1);
        store.write("new", false, payload, now); QVERIFY(readRecord(&store, "new").valid); QVERIFY(!readRecord(&store, "old").valid);
        const auto path = dir.filePath("metadata/json/" + MetadataCache::digest("new") + ".json");
        QFile broken(path); QVERIFY(broken.open(QIODevice::WriteOnly)); broken.write("broken"); broken.close();
        QVERIFY(!readRecord(&store, "new").valid);
        store.write("late", false, payload, now);
        bool done = false; store.clear([&](QString error) { QVERIFY(error.isEmpty()); done = true; }); QTRY_VERIFY(done);
        QVERIFY(!readRecord(&store, "late").valid);
        store.write("cover1", true, imageBytes(), now); QVERIFY(readRecord(&store, "cover1", true).valid);
        now = now.addSecs(1); store.write("cover2", true, imageBytes(Qt::blue), now);
        QVERIFY(readRecord(&store, "cover2", true).valid); QVERIFY(!readRecord(&store, "cover1", true).valid);
        const auto current = readRecord(&store, "cover2", true);
        store.setPins("test", {"cover2"});
        store.setCoverFiles({QFileInfo(current.localUrl.toLocalFile()).fileName()});
        bool completed = false; MetadataCache::Record session;
        store.write("cover2", true, imageBytes(Qt::red), now, [&](MetadataCache::Record value) { session = value; completed = true; });
        QTRY_VERIFY(completed); QVERIFY(session.localUrl.isEmpty());
        QVERIFY(QFileInfo::exists(current.localUrl.toLocalFile()));
        QCOMPARE(readRecord(&store, "cover2", true).data, current.data);
    }
    void storeWriteFailureRemainsUsable() {
        MockBangumiServer server; QTemporaryDir dir; QDir().mkpath(dir.filePath("metadata"));
        QFile blocker(dir.filePath("metadata/json")); QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
        AnimeMetadataService service(dir.path()); service.setBaseUrl(server.url()); service.fetchDetail(101);
        QTRY_VERIFY(!service.loadingDetail()); QVERIFY(!service.currentDetail().isEmpty()); QVERIFY(service.detailError().isEmpty());
        QVERIFY(!readRecord(service.cacheStore(), server.url() + "|1|101").valid);
    }
    void searchRequestAndFields() {
        MockBangumiServer server;
        QTemporaryDir dir;
        AnimeMetadataService service(dir.path());
        service.setBaseUrl(server.url());
        service.searchBangumi(QStringLiteral("  中文 / ?#  "));
        QVERIFY(service.searching());
        QTRY_VERIFY(!service.searching());
        QVERIFY2(service.searchError().isEmpty(), qPrintable(service.searchError()));
        QCOMPARE(server.searchMethod, QByteArray("POST"));
        QCOMPARE(server.searchPayload.value("keyword").toString(), QStringLiteral("中文 / ?#"));
        QCOMPARE(server.searchPayload.value("sort").toString(), QString("match"));
        QCOMPARE(server.searchPayload.value("filter").toObject().value("type").toArray(), QJsonArray{2});
        QVERIFY(!server.searchPayload.value("filter").toObject().value("nsfw").toBool(true));
        const QUrlQuery query(QUrl(server.url() + QString::fromUtf8(server.searchTarget)));
        QCOMPARE(query.queryItemValue("limit"), QString("20"));
        QCOMPARE(query.queryItemValue("offset"), QString("0"));
        QCOMPARE(service.searchTotal(), 45);
        QCOMPARE(service.searchResults().size(), 1);
        const auto item = service.searchResults().first().toMap();
        QCOMPARE(item.value("id").toLongLong(), 101);
        QCOMPARE(item.value("title").toString(), QStringLiteral("测试番剧详情"));
        QCOMPARE(item.value("originalTitle").toString(), QString("Original Subject"));
        QCOMPARE(item.value("score").toString(), QString("8.8"));
        QCOMPARE(item.value("airDate").toString(), QString("2026-10-01"));
    }
    void searchReplacementClearAndInputLimits() {
        MockBangumiServer server;
        QTemporaryDir dir;
        AnimeMetadataService service(dir.path());
        service.setBaseUrl(server.url());
        server.searchDelay = 150;
        service.searchBangumi("old");
        QTRY_COMPARE(server.searchRequests, 1);
        server.searchDelay = 0;
        service.searchBangumi("new");
        QTRY_VERIFY(!service.searching());
        QCOMPARE(service.searchResults().first().toMap().value("id").toLongLong(), 202);
        QTest::qWait(180);
        QCOMPARE(service.searchKeyword(), QString("new"));
        QCOMPARE(service.searchResults().first().toMap().value("id").toLongLong(), 202);
        server.searchDelay = 150;
        service.searchBangumi("cancel");
        QTRY_COMPARE(server.searchRequests, 3);
        service.clearSearch();
        QTest::qWait(180);
        QVERIFY(!service.searching());
        QVERIFY(service.searchKeyword().isEmpty());
        QVERIFY(service.searchResults().isEmpty());
        QVERIFY(service.searchError().isEmpty());
        QCOMPARE(service.searchTotal(), 0);
        service.searchBangumi(" \n ");
        service.searchBangumi(QString(513, QLatin1Char('a')));
        QVERIFY(!service.searchError().isEmpty());
        QVERIFY(!service.searching());
        QCOMPARE(server.searchRequests, 3);
        service.clearSearch();
        QVERIFY(service.searchError().isEmpty());
    }
    void searchErrorsAndEmptyResults_data() {
        QTest::addColumn<QByteArray>("body");
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("error");
        QTest::newRow("http-error") << QByteArray("{}") << 503 << true;
        QTest::newRow("invalid-json") << QByteArray("broken") << 200 << true;
        QTest::newRow("invalid-shape") << QByteArray("{}") << 200 << true;
        QTest::newRow("invalid-total") << QByteArray(R"({"data":[],"total":0.5})") << 200 << true;
        QTest::newRow("invalid-item") << QByteArray(R"({"data":[{"id":0}],"total":1})") << 200 << true;
        QTest::newRow("empty") << QByteArray(R"({"data":[],"total":0})") << 200 << false;
        QJsonArray items;
        for (int i = 0; i < 21; ++i) items.append(QJsonObject{{"id", i + 1}, {"name", "test"}, {"type", 2}});
        QTest::newRow("over-item-limit") << QJsonDocument(QJsonObject{{"data", items}, {"total", 21}}).toJson() << 200 << true;
        QTest::newRow("over-response-limit") << QByteArray(16 * 1024 * 1024 + 1, ' ') << 200 << true;
    }
    void searchErrorsAndEmptyResults() {
        QFETCH(QByteArray, body);
        QFETCH(int, status);
        QFETCH(bool, error);
        MockBangumiServer server;
        server.searchBody = body;
        server.searchStatus = status;
        QTemporaryDir dir;
        AnimeMetadataService service(dir.path());
        service.setBaseUrl(server.url());
        service.searchBangumi("test");
        QTRY_VERIFY_WITH_TIMEOUT(!service.searching(), 5000);
        QCOMPARE(!service.searchError().isEmpty(), error);
        QVERIFY(service.searchResults().isEmpty());
        server.searchBody = R"({"data":[],"total":0})";
        server.searchStatus = 200;
        service.searchBangumi(service.searchKeyword(), true);
        QTRY_VERIFY(!service.searching());
        QVERIFY(service.searchError().isEmpty());
    }
    void searchFilteringAndTimeout() {
        MockBangumiServer server;
        server.searchBody = R"({"total":5,"data":[{"id":1,"name":"original","type":2},
            {"id":1,"name":"duplicate","type":2},{"id":2,"name":"book","type":1},
            {"id":3,"name":"restricted","type":2,"nsfw":true},{"id":4,"type":2}]})";
        QTemporaryDir dir;
        AnimeMetadataService service(dir.path(), nullptr, 60);
        service.setBaseUrl(server.url());
        service.searchBangumi("test");
        QTRY_VERIFY(!service.searching());
        QCOMPARE(service.searchResults().size(), 1);
        QCOMPARE(service.searchResults().first().toMap().value("title").toString(), QString("original"));
        server.searchDelay = 250;
        service.searchBangumi("timeout");
        QTRY_VERIFY(!service.searching());
        QVERIFY(!service.searchError().isEmpty());
        QTest::qWait(280);
        QVERIFY(service.searchResults().isEmpty());
        // Destroying the service must cancel pending requests and queued parsing.
        auto pending = std::make_unique<AnimeMetadataService>(dir.path());
        pending->setBaseUrl(server.url());
        pending->searchBangumi("pending");
        QTRY_COMPARE(server.searchRequests, 3);
        pending.reset();
        QTest::qWait(280);
    }
    void detailCancellationAndReplacement() {
        MockBangumiServer server;
        QTemporaryDir dir;
        AnimeMetadataService service(dir.path());
        service.setBaseUrl(server.url());
        server.detailDelay = 100;
        server.episodeDelay = 100;
        service.fetchDetail(101);
        QTRY_COMPARE(server.subjectRequests, 1);
        service.clearDetail();
        QTest::qWait(150);
        QVERIFY(service.currentDetail().isEmpty());
        QVERIFY(service.detailError().isEmpty());
        QCOMPARE(server.episodeRequests, 1);
        server.detailDelay = 0;
        server.episodeDelay = 100;
        service.fetchDetail(101);
        QTRY_COMPARE(server.episodeRequests, 2);
        service.clearDetail();
        QTest::qWait(150);
        QVERIFY(service.currentDetail().isEmpty());
        QVERIFY(!service.loadingDetail());
        service.fetchDetail(101);
        service.fetchDetail(202);
        QTRY_VERIFY(!service.loadingDetail());
        QCOMPARE(service.currentDetail().value("id").toLongLong(), 202);
    }
    void detailPaginationErrorsAndRetry() {
        MockBangumiServer server;
        QTemporaryDir dir;
        AnimeMetadataService service(dir.path());
        service.setBaseUrl(server.url());
        server.episodeCount = 205;
        service.fetchDetail(101);
        QTRY_VERIFY(!service.loadingDetail());
        QCOMPARE(service.currentDetail().value("episodes").toList().size(), 205);
        QCOMPARE(server.episodeRequests, 3);
        server.episodeStatus = 503;
        service.fetchDetail(101, true);
        QTRY_VERIFY(!service.loadingDetail());
        QCOMPARE(service.currentDetail().value("id").toLongLong(), 101);
        QVERIFY(!service.detailError().isEmpty());
        server.episodeStatus = 200;
        service.fetchDetail(101, true);
        QTRY_VERIFY(!service.loadingDetail());
        QVERIFY(service.detailError().isEmpty());
        QCOMPARE(service.currentDetail().value("episodes").toList().size(), 205);
    }
    void cacheFreshnessAndInvalidResponsePreservation() {
        MockBangumiServer server;
        QTemporaryDir dir;
        TestClock now(QDateTime::currentDateTimeUtc());
        MetadataCache::Policy policy;
        policy.now = [&] { return now.value(); };
        const auto key = server.url() + "|0|";
        const auto path = QDir(dir.path()).filePath("metadata/json/" + MetadataCache::digest(key.toUtf8()) + ".json");
        {
            AnimeMetadataService service(dir.path(), policy);
            service.setBaseUrl(server.url());
            service.refreshCalendar(true);
            QTRY_VERIFY(!service.loading());
            QCOMPARE(service.weeklyCalendar().size(), 7);
        }
        const int count = server.calendarRequests;
        {
            AnimeMetadataService service(dir.path(), policy);
            service.setBaseUrl(server.url());
            service.refreshCalendar(false);
            QTRY_VERIFY(!service.loading());
            QCOMPARE(server.calendarRequests, count);
            service.refreshCalendar(true);
            QTRY_VERIFY(!service.loading());
            QCOMPARE(server.calendarRequests, count + 1);
            server.calendarBody = "not json";
            service.refreshCalendar(true);
            QTRY_VERIFY(!service.loading());
            QVERIFY(!service.error().isEmpty());
            QCOMPARE(service.weeklyCalendar().size(), 7);
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(QJsonDocument(QJsonDocument::fromJson(file.readAll()).object().value("payload").toArray()).toJson(QJsonDocument::Compact), mockCalendarJson());
        }
        now = now.addDays(2);
        server.calendarBody.clear();
        AnimeMetadataService stale(dir.path(), policy);
        stale.setBaseUrl(server.url());
        stale.refreshCalendar(false);
        QTRY_VERIFY(!stale.loading());
        QCOMPARE(server.calendarRequests, count + 3);
    }
    void detailTimeout() {
        QTest::failOnWarning(QRegularExpression(".*device not open.*"));
        MockBangumiServer server;
        server.detailDelay = 200;
        QTemporaryDir dir;
        AnimeMetadataService service(dir.path(), nullptr, 40);
        service.setBaseUrl(server.url());
        service.fetchDetail(101);
        QTRY_VERIFY(!service.loadingDetail());
        QVERIFY(!service.detailError().isEmpty());
        QTest::qWait(220);
        QVERIFY(service.currentDetail().isEmpty());
    }
    void testCalendarParsingAndAggregation() {
        MockBangumiServer server;
        QTemporaryDir dir;
        AnimeMetadataService service(dir.path());
        service.setBaseUrl(server.url());

        QSignalSpy spy(&service, &AnimeMetadataService::calendarChanged);
        service.refreshCalendar(true);

        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
        QVERIFY(!service.loading());
        QVERIFY(service.error().isEmpty());

        // Weekly calendar must have 7 days
        QCOMPARE(service.weeklyCalendar().size(), 7);

        // Airing today must not be empty
        const auto today = service.airingToday();
        QVERIFY(!today.isEmpty());
        for (const auto& itemVal : today) {
            const auto item = itemVal.toMap();
            QVERIFY(item.value(QStringLiteral("id")).toLongLong() > 0);
            QVERIFY(!item.value(QStringLiteral("title")).toString().isEmpty());
            // Cover URL should be normalized to https
            const auto cover = item.value(QStringLiteral("cover")).toString();
            QVERIFY(cover.startsWith(QStringLiteral("https://")));
        }

        // Top rated must be sorted descending by rawScore
        const auto topRated = service.topRated();
        QVERIFY(!topRated.isEmpty());
        for (int i = 1; i < topRated.size(); ++i) {
            QVERIFY(topRated[i - 1].toMap().value(QStringLiteral("rawScore")).toDouble() >=
                    topRated[i].toMap().value(QStringLiteral("rawScore")).toDouble());
        }

        // Hero anime should be valid
        const auto hero = service.heroAnime();
        QVERIFY(!hero.value(QStringLiteral("title")).toString().isEmpty());
        QVERIFY(hero.value(QStringLiteral("id")).toLongLong() > 0);
    }

    void testDiskCachePersistence() {
        MockBangumiServer server;
        QTemporaryDir dir;

        {
            AnimeMetadataService service(dir.path());
            service.setBaseUrl(server.url());
            QSignalSpy spy(&service, &AnimeMetadataService::calendarChanged);
            service.refreshCalendar(true);
            QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 5000);
        }

        // Verify file exists
        const auto key = server.url() + "|0|";
        QFile file(QDir(dir.path()).filePath("metadata/json/" + MetadataCache::digest(key.toUtf8()) + ".json"));
        QVERIFY(file.exists());
        QVERIFY(file.size() > 100);

        // Restart with the same source, now unavailable. Fresh entries do not access it.
        const auto count = server.calendarRequests;
        server.status = 503;
        AnimeMetadataService offlineService(dir.path());
        offlineService.setBaseUrl(server.url());
        offlineService.refreshCalendar();
        QTRY_VERIFY(!offlineService.loading());
        QCOMPARE(offlineService.weeklyCalendar().size(), 7);
        QCOMPARE(server.calendarRequests, count);
        QVERIFY(!offlineService.airingToday().isEmpty());
        QVERIFY(!offlineService.heroAnime().isEmpty());
    }

    void testSubjectDetailAndEpisodes() {
        MockBangumiServer server;
        QTemporaryDir dir;
        AnimeMetadataService service(dir.path());
        service.setBaseUrl(server.url());

        QSignalSpy spy(&service, &AnimeMetadataService::detailChanged);
        service.fetchDetail(101);

        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1 && !service.loadingDetail(), 5000);

        const auto detail = service.currentDetail();
        QCOMPARE(detail.value(QStringLiteral("id")).toLongLong(), 101);
        QCOMPARE(detail.value(QStringLiteral("title")).toString(), QStringLiteral("测试番剧详情"));
        QCOMPARE(detail.value(QStringLiteral("score")).toString(), QStringLiteral("8.8"));
        QCOMPARE(detail.value(QStringLiteral("eps")).toInt(), 12);

        const auto eps = detail.value(QStringLiteral("episodes")).toList();
        QCOMPARE(eps.size(), 4);
        QCOMPARE(eps[0].toMap().value(QStringLiteral("sort")).toInt(), 1);
        QCOMPARE(eps[0].toMap().value(QStringLiteral("title")).toString(), QStringLiteral("第 1 话 冒险旅程"));
    }

    void testErrorHandling() {
        MockBangumiServer server;
        server.status = 500;
        QTemporaryDir dir;
        AnimeMetadataService service(dir.path());
        service.setBaseUrl(server.url());

        QSignalSpy spy(&service, &AnimeMetadataService::stateChanged);
        service.refreshCalendar(true);

        QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1 && !service.loading(), 5000);
        QVERIFY(!service.error().isEmpty());
    }
};

QTEST_MAIN(AnimeMetadataTests)
#include "AnimeMetadataTests.moc"
