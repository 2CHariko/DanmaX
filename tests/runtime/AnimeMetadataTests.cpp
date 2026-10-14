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
                    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
                    const auto target = request->split(' ').value(1);

                    QByteArray body;
                    int responseStatus = status;
                    int delay = 0;
                    if (status != 200) {
                        body = "Internal Server Error";
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
