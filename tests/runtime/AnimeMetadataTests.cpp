#include "infrastructure/AnimeMetadataService.h"
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
    QJsonObject searchPayload;
    QByteArray searchTarget, searchMethod, searchBody;
    QByteArray calendarBody;
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

                    QByteArray body;
                    int responseStatus = status;
                    int delay = 0;
                    if (status != 200) {
                        body = "Internal Server Error";
                    } else if (target.startsWith("/v0/search/subjects")) {
                        ++searchRequests;
                        searchMethod = request->split(' ').value(0);
                        searchTarget = target;
                        searchPayload = QJsonDocument::fromJson(request->mid(headerEnd)).object();
                        auto item = QJsonDocument::fromJson(mockSubjectJson(searchPayload.value("keyword").toString() == "new" ? 202 : 101)).object();
                        item["type"] = 2;
                        body = searchBody.isEmpty() ? QJsonDocument(QJsonObject{{"total", 45}, {"data", QJsonArray{item}}}).toJson() : searchBody;
                        delay = searchDelay;
                        responseStatus = searchStatus;
                    } else if (target.startsWith("/calendar")) {
                        ++calendarRequests;
                        body = calendarBody.isEmpty() ? mockCalendarJson() : calendarBody;
                    } else if (target.startsWith("/v0/subjects/")) {
                        ++subjectRequests;
                        body = mockSubjectJson(target.mid(QByteArray("/v0/subjects/").size()).toLongLong());
                        delay = detailDelay;
                    } else if (target.startsWith("/v0/episodes")) {
                        ++episodeRequests;
                        const QUrlQuery query(QUrl(QStringLiteral("http://localhost") + QString::fromUtf8(target)));
                        body = mockEpisodesJson(query.queryItemValue("subject_id").toLongLong(), episodeCount, query.queryItemValue("offset").toInt());
                        delay = episodeDelay;
                        responseStatus = episodeStatus;
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

class AnimeMetadataTests : public QObject {
    Q_OBJECT
private slots:
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
        service.searchBangumi(service.searchKeyword());
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
        service.fetchDetail(101);
        QTRY_COMPARE(server.subjectRequests, 1);
        service.clearDetail();
        QTest::qWait(150);
        QVERIFY(service.currentDetail().isEmpty());
        QVERIFY(service.detailError().isEmpty());
        QCOMPARE(server.episodeRequests, 0);
        server.detailDelay = 0;
        server.episodeDelay = 100;
        service.fetchDetail(101);
        QTRY_COMPARE(server.episodeRequests, 1);
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
        service.fetchDetail(101);
        QTRY_VERIFY(!service.loadingDetail());
        QCOMPARE(service.currentDetail().value("id").toLongLong(), 101);
        QVERIFY(!service.detailError().isEmpty());
        server.episodeStatus = 200;
        service.fetchDetail(101);
        QTRY_VERIFY(!service.loadingDetail());
        QVERIFY(service.detailError().isEmpty());
        QCOMPARE(service.currentDetail().value("episodes").toList().size(), 205);
    }
    void cacheFreshnessAndInvalidResponsePreservation() {
        MockBangumiServer server;
        QTemporaryDir dir;
        const auto path = QDir(dir.path()).filePath("metadata/calendar.json");
        {
            AnimeMetadataService service(dir.path());
            service.setBaseUrl(server.url());
            service.refreshCalendar(true);
            QTRY_VERIFY(!service.loading());
            QCOMPARE(service.weeklyCalendar().size(), 7);
        }
        const int count = server.calendarRequests;
        {
            AnimeMetadataService service(dir.path());
            service.setBaseUrl(server.url());
            service.refreshCalendar(false);
            QTest::qWait(50);
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
            QCOMPARE(file.readAll(), mockCalendarJson());
        }
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2), QFileDevice::FileModificationTime));
        file.close();
        server.calendarBody.clear();
        AnimeMetadataService stale(dir.path());
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
        QFile file(QDir(dir.path()).filePath(QStringLiteral("metadata/calendar.json")));
        QVERIFY(file.exists());
        QVERIFY(file.size() > 100);

        // Construct a new instance pointing to invalid server; should load from disk cache immediately
        AnimeMetadataService offlineService(dir.path());
        offlineService.setBaseUrl(QStringLiteral("http://127.0.0.1:1")); // Offline/closed port

        // Data should already be populated from constructor disk cache load
        QCOMPARE(offlineService.weeklyCalendar().size(), 7);
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
