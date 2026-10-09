#include "application/AppController.h"
#include "application/DanmakuLibrary.h"
#include "infrastructure/OnlineDanmakuData.h"
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QSslSocket>
#include <QSslConfiguration>
#include <QSslKey>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray commentBody(const QString& text = QStringLiteral("测试弹幕")) {
    return QJsonDocument(QJsonObject{{"comments", QJsonArray{
        QJsonObject{{"p", "2,1,16777215,user"}, {"m", text}},
        QJsonObject{{"p", "1,5,255,user"}, {"m", "顶部"}}
    }}}).toJson(QJsonDocument::Compact);
}
struct Response { int status{200}; QByteArray body; int delay{}; QByteArray headers; };
class FakeServer final : public QTcpServer {
  public:
    bool tls{};
    QSslConfiguration ssl;
    QList<QByteArray> targets;
    QList<QByteArray> requests;
    std::function<Response(QByteArray)> response;
    explicit FakeServer(bool encrypted = false) : tls(encrypted) {
        response = [](QByteArray target) {
            if (target.contains("/search/anime")) return Response{200, R"({"animes":[{"animeId":42,"animeTitle":"测试动画"}]})"};
            if (target.contains("/bangumi/")) return Response{200, R"({"bangumi":{"episodes":[{"episodeId":42001,"episodeTitle":"第1话"}]}})"};
            return Response{200, commentBody()};
        };
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
                    buffer->append(socket->readAll());
                    if (!buffer->contains("\r\n\r\n")) return;
                    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
                    const auto target = buffer->split(' ').value(1);
                    targets.append(target); requests.append(*buffer);
                    const auto output = response(target);
                    if (output.delay < 0) return;
                    QTimer::singleShot(output.delay, this, [guard = QPointer<QTcpSocket>(socket), output] {
                        if (!guard || guard->state() != QAbstractSocket::ConnectedState) return;
                        guard->write("HTTP/1.1 " + QByteArray::number(output.status) + " Result\r\nContent-Type: application/json\r\n" +
                            (output.headers.isEmpty() ? "Content-Length: " + QByteArray::number(output.body.size()) + "\r\n" : output.headers) +
                            "Connection: close\r\n\r\n" + output.body);
                        guard->disconnectFromHost();
                    });
                });
            }
        });
        listen(QHostAddress::LocalHost, 0);
    }
    QString base() const { return QStringLiteral("%1://127.0.0.1:%2/prefix").arg(tls ? "https" : "http").arg(serverPort()); }
  protected:
    void incomingConnection(qintptr descriptor) override {
        if (!tls) { QTcpServer::incomingConnection(descriptor); return; }
        auto* socket = new QSslSocket(this);
        socket->setSslConfiguration(ssl);
        connect(socket, &QSslSocket::encrypted, this, [this, socket] { addPendingConnection(socket); emit newConnection(); });
        connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        socket->setSocketDescriptor(descriptor);
        socket->startServerEncryption();
    }
};
QVariantMap metadata() {
    return {{"server", "https://example.test/prefix"}, {"animeId", "42"}, {"episodeId", "42001"},
            {"animeTitle", "测试动画"}, {"episodeTitle", "第1话"}, {"downloadedAt", "2026-10-08T00:00:00.000Z"}};
}
void writeBytes(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (file.open(QIODevice::WriteOnly)) file.write(bytes);
}
} // namespace

class OnlineDanmakuTests final : public QObject {
    Q_OBJECT
  private slots:
    void orderedSettingsRoundTrip() {
        QTemporaryDir root;
        writeBytes(root.filePath("settings.ini"), "[Meta]\nformatVersion=1\n[Online]\ndanmakuServer=\"https://old.test\"\n");
        SettingsStore settings(root.path());
        QCOMPARE(settings.values()["danmakuServers"].toStringList(), QStringList{"https://danmaku-api.152468.xyz"});
        QVERIFY(!settings.setValue("danmakuServer", "https://old.test"));
        const QStringList expected{"https://b.test/prefix", "http://a.test:8080"};
        QVERIFY(settings.setValue("danmakuServers", QStringList{" https://B.test:443/prefix/ ", "http://a.test:8080/"}));
        QCOMPARE(settings.values()["danmakuServers"].toStringList(), expected);
        QVERIFY(!settings.setValue("danmakuServers", QStringList{"https://a.test", "https://A.test/"}));
        QVERIFY(!settings.setValue("danmakuServers", QVariantList{42}));
        QVERIFY(!settings.setValue("danmakuServers", QStringList{""}));
        QCOMPARE(settings.values()["danmakuServers"].toStringList(), expected);
        SettingsStore restored(root.path());
        QCOMPARE(restored.values()["danmakuServers"].toStringList(), expected);
        QVERIFY(settings.setValue("danmakuServers", QStringList{}));
        SettingsStore empty(root.path());
        QVERIFY(empty.values()["danmakuServers"].toStringList().isEmpty());
        QVERIFY(settings.reset());
        SettingsStore reset(root.path());
        QCOMPARE(reset.values(), SettingsStore::defaults());
        writeBytes(root.filePath("settings.ini"), "[Meta]\nformatVersion=1\n[Online]\ndanmakuServers=\"not JSON\"\n");
        SettingsStore invalid(root.path());
        QVERIFY(!invalid.error().isEmpty());
        QCOMPARE(invalid.values()["danmakuServers"], SettingsStore::defaults()["danmakuServers"]);
    }
    void orderedFallback_data() {
        QTest::addColumn<int>("failure");
        for (int i = 0; i < 7; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void orderedFallback() {
        QFETCH(int, failure);
        QTemporaryDir root; FakeServer first, second, unused;
        first.response = [failure](QByteArray target) {
            if (failure == 0) return Response{503, "failed"};
            if (failure == 1) return Response{401, "auth"};
            if (failure == 2) return Response{200, "{"};
            if (failure == 3) return Response{200, R"({"success":false})"};
            if (failure == 4) return Response{200, "", -1};
            if (failure == 5) return Response{200, "", 0, "Content-Length: 67108865\r\n"};
            if (target.contains("/search/")) return Response{200, R"({"animes":[]})"};
            if (target.contains("/bangumi/")) return Response{200, R"({"episodes":[]})"};
            return Response{200, R"({"comments":[]})"};
        };
        QStringList order;
        const auto bad = first.response, good = second.response;
        first.response = [&](QByteArray target) { order.append("first"); return bad(target); };
        second.response = [&](QByteArray target) { order.append("second"); return good(target); };
        DanmakuLibrary library(root.path(), nullptr, 250);
        library.setServers({first.base(), second.base(), unused.base()});
        QVERIFY(first.targets.isEmpty()); QVERIFY(second.targets.isEmpty());
        library.searchAnime("测试"); QTRY_VERIFY(!library.busy()); QCOMPARE(library.animes().size(), 1);
        library.selectAnime("42"); QTRY_VERIFY(!library.busy()); QCOMPARE(library.episodes().size(), 1);
        library.selectEpisode("42001");
        QSignalSpy ready(&library, &DanmakuLibrary::sourceReady);
        library.downloadEpisode(true); QTRY_COMPARE(ready.count(), 1);
        const auto result = library.takeReadyResult(); QVERIFY(result);
        QCOMPARE(result->metadata["server"].toString(), second.base());
        QCOMPARE(library.activeServer(), second.base());
        QCOMPARE(order, (QStringList{"first", "second", "first", "second", "first", "second"}));
        QVERIFY(unused.targets.isEmpty());
        QTRY_VERIFY(!library.scanning()); QVERIFY(library.selectedCached());
        library.downloadEpisode(); QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(order.size(), 6); // Second service cache is used before any network request.
        library.downloadEpisode(true); QTRY_COMPARE(ready.count(), 3); QCOMPARE(order.size(), 8);
        auto primary = parseOnlineDanmaku(commentBody("primary")); primary.metadata = result->metadata;
        primary.metadata["server"] = first.base();
        QVERIFY(saveCachedDanmaku(root.filePath("danmaku"), primary).isEmpty());
        library.downloadEpisode(); QTRY_COMPARE(ready.count(), 4);
        QCOMPARE(library.takeReadyResult()->metadata["server"].toString(), first.base());
        QCOMPARE(order.size(), 8);
    }
    void fallbackCancellationAndExhaustion() {
        QTemporaryDir root; FakeServer first, second;
        first.response = [](QByteArray) { return Response{503, "failed", 100}; };
        second.response = [](QByteArray) { return Response{403, "auth"}; };
        DanmakuLibrary library(root.path()); library.setServers({first.base(), second.base()});
        library.searchAnime("cancel"); QTRY_COMPARE(first.targets.size(), 1);
        library.cancel(); QTest::qWait(150); QVERIFY(second.targets.isEmpty()); QVERIFY(!library.busy());
        library.searchAnime("change"); QTRY_COMPARE(first.targets.size(), 2);
        library.setServers({second.base()}); QTest::qWait(150); QVERIFY(second.targets.isEmpty());
        library.setServers({first.base(), second.base()});
        library.searchAnime("all fail"); QTRY_VERIFY(!library.busy());
        QVERIFY(library.error().contains(first.base())); QVERIFY(library.error().contains(second.base()));
        QVERIFY(library.error().contains("503")); QVERIFY(library.error().contains("认证"));
        const auto count = first.targets.size();
        library.searchAnime(" "); QVERIFY(!library.busy()); QCOMPARE(first.targets.size(), count);
        first.close(); // Connection refused also falls back.
        second.response = [](QByteArray) { return Response{200, R"({"animes":[{"animeId":42,"animeTitle":"ok"}]})"}; };
        library.searchAnime("network"); QTRY_VERIFY(!library.busy()); QCOMPARE(library.animes().size(), 1);
        library.setServers({second.base()});
        const auto before = second.targets.size();
        const auto connection = connect(&library, &DanmakuLibrary::changed, &library, [&] {
            if (library.busy() && library.status().contains("（1/1）")) library.cancel();
        });
        library.searchAnime("cancel from status"); QTest::qWait(80);
        QVERIFY(!library.busy()); QCOMPARE(second.targets.size(), before);
        disconnect(connection);
    }
    void serverValidationAndTls() {
        QCOMPARE(normalizeDanmakuServer(" https://EXAMPLE.test:443/prefix/ "), QString("https://example.test/prefix"));
        for (const auto& address : {"file:///x", "localhost", "https://a.test/?token=x", "https://user:pw@a.test", "https://a.test/#x"})
            QVERIFY(normalizeDanmakuServer(address).isEmpty());
        QVERIFY(QSslSocket::availableBackends().contains("schannel"));
        QVERIFY(QSslSocket::supportsSsl());
        QTemporaryDir root;
        SettingsStore settings(root.path());
        QVERIFY(!settings.setValue("danmakuServers", QStringList{"not a URL"}));
        QVERIFY(settings.setValue("danmakuServers", QStringList{"https://example.test/prefix/"}));
        QCOMPARE(settings.values().value("danmakuServers").toStringList(), QStringList{"https://example.test/prefix"});
    }
    void parserBoundariesAndDeduplication() {
        QJsonArray array{
            QJsonObject{{"p", "2,1,16777215,u"}, {"m", "重复"}},
            QJsonObject{{"p", "1,5,255,u"}, {"m", "顶部"}},
            QJsonObject{{"p", "2,1,16777215,other"}, {"m", "重复"}},
            QJsonObject{{"p", "3,4,null,u"}, {"m", "底部\n换行"}},
            QJsonObject{{"p", "3,6,1,u"}, {"m", "反向"}},
            QJsonObject{{"p", "3,7,1,u"}, {"m", "高级"}},
            QJsonObject{{"p", "-1,1,1,u"}, {"m", "负时间"}},
            QJsonObject{{"p", "nan,1,1,u"}, {"m", "非法时间"}},
            QJsonObject{{"p", "604801,1,1,u"}, {"m", "越界"}},
            QJsonObject{{"p", "1,1,16777216,u"}, {"m", "非法颜色"}},
            QJsonObject{{"p", "1,1,1,u"}, {"m", QString(513, 'x')}},
            QJsonObject{{"p", "1,1,1,u"}, {"m", "  "}},
            QJsonObject{{"p", "1,1"}, {"m", "缺字段"}},
            QJsonValue(1)
        };
        const auto result = parseOnlineDanmaku(QJsonDocument(QJsonObject{{"comments", array}}).toJson());
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.items.size(), std::size_t(3));
        QCOMPARE(result.items[0].time, 1.0);
        QCOMPARE(result.items[0].mode, danmaku::Mode::Top);
        QCOMPARE(result.items[2].color, std::uint32_t(0xffffff));
        QCOMPARE(result.items[2].fontSize, 25);
        QCOMPARE(QString::fromUtf8(result.items[2].text), QString("底部 换行"));
        QCOMPARE(result.duplicates, 1); QCOMPARE(result.unsupported, 2); QCOMPARE(result.invalid, 8);
        QVERIFY(!parseOnlineDanmaku("{}").error.isEmpty());
        QVERIFY(!parseOnlineDanmaku("{").error.isEmpty());
        QVERIFY(!parseOnlineDanmaku(R"({"comments":[]})").error.isEmpty());
        QVERIFY(!parseOnlineDanmaku(R"({"success":false,"comments":[]})").error.isEmpty());
        std::stop_source source; source.request_stop();
        QVERIFY(parseOnlineDanmaku(commentBody(), source.get_token()).cancelled);
        QByteArray tooMany = "{\"comments\":[";
        for (int i = 0; i < 1000001; ++i) { if (i) tooMany += ','; tooMany += "null"; }
        tooMany += "]}";
        QVERIFY(parseOnlineDanmaku(tooMany).error.contains("100 万"));
        QVERIFY(parseOnlineDanmaku(QByteArray(64 * 1024 * 1024 + 1, ' ')).error.contains("64 MiB"));
    }
    void httpsWithProjectLocalCertificate() {
        QFile certificateFile(QFINDTESTDATA("../fixtures/online-server-cert.pem")); QVERIFY(certificateFile.open(QIODevice::ReadOnly));
        QFile keyFile(QFINDTESTDATA("../fixtures/online-server-key.pem")); QVERIFY(keyFile.open(QIODevice::ReadOnly));
        const QSslCertificate certificate(certificateFile.readAll());
        const QSslKey key(keyFile.readAll(), QSsl::Rsa);
        QVERIFY(!certificate.isNull()); QVERIFY(!key.isNull());
        FakeServer server(true); server.ssl = QSslConfiguration::defaultConfiguration();
        server.ssl.setLocalCertificate(certificate); server.ssl.setPrivateKey(key);
        server.ssl.setPeerVerifyMode(QSslSocket::VerifyNone); // Mock server does not require a client certificate.
        const auto original = QSslConfiguration::defaultConfiguration();
        auto trusted = original; trusted.addCaCertificate(certificate);
        QSslConfiguration::setDefaultConfiguration(trusted);
        QTemporaryDir root;
        DanmakuLibrary library(root.path()); library.setServers({server.base()}); library.searchAnime("HTTPS");
        QTRY_VERIFY_WITH_TIMEOUT(!library.busy(), 15000);
        const auto error = library.error(); const auto size = library.animes().size();
        QSslConfiguration::setDefaultConfiguration(original);
        QVERIFY2(error.isEmpty(), qPrintable(error)); QCOMPARE(size, 1);
        // Production uses normal certificate verification; an untrusted server must fail.
        DanmakuLibrary untrusted(root.filePath("untrusted")); untrusted.setServers({server.base()}); untrusted.searchAnime("HTTPS");
        QTRY_VERIFY_WITH_TIMEOUT(!untrusted.busy(), 15000); QVERIFY(!untrusted.error().isEmpty());
    }
    void cacheIntegrityAndAtomicReplacement() {
        QTemporaryDir root;
        auto result = parseOnlineDanmaku(commentBody()); result.metadata = metadata();
        const auto directory = root.filePath("danmaku");
        const auto key = danmakuCacheKey(result.metadata["server"].toString(), 42001);
        QVERIFY(key != danmakuCacheKey("https://other.test/prefix", 42001));
        QVERIFY(saveCachedDanmaku(directory, result).isEmpty());
        auto loaded = readCachedDanmaku(directory, key);
        QVERIFY2(loaded.error.isEmpty(), qPrintable(loaded.error));
        QCOMPARE(loaded.items.size(), result.items.size());
        auto replacement = parseOnlineDanmaku(commentBody("新版")); replacement.metadata = metadata();
        std::stop_source cancelled; cancelled.request_stop();
        saveCachedDanmaku(directory, replacement, cancelled.get_token());
        QCOMPARE(readCachedDanmaku(directory, key).items.back().text, result.items.back().text);
        QVERIFY(saveCachedDanmaku(directory, replacement).isEmpty());
        QCOMPARE(QString::fromUtf8(readCachedDanmaku(directory, key).items.back().text), QString("新版"));
        QFile file(QDir(directory).filePath(key + ".json")); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto valid = file.readAll(); file.close();
        auto object = QJsonDocument::fromJson(valid).object(); object["version"] = 99;
        writeBytes(file.fileName(), QJsonDocument(object).toJson());
        auto entries = listCachedDanmaku(directory);
        QCOMPARE(entries.size(), 1); QVERIFY(!entries[0].toMap()["valid"].toBool());
        QVERIFY(readCachedDanmaku(directory, key).error.contains("版本"));
        writeBytes(file.fileName(), "broken");
        QVERIFY(!listCachedDanmaku(directory)[0].toMap()["valid"].toBool());
        QVERIFY(!readCachedDanmaku(directory, "../escape").error.isEmpty());
        QVERIFY(!deleteCachedDanmaku(directory, "../escape").isEmpty());
        writeBytes(root.filePath("file-instead-of-directory"), "x");
        QVERIFY(!saveCachedDanmaku(root.filePath("file-instead-of-directory"), result).isEmpty());
        QVERIFY(deleteCachedDanmaku(directory, key).isEmpty()); QVERIFY(listCachedDanmaku(directory).isEmpty());
    }
    void downloadCacheAndOfflineReload() {
        QTemporaryDir root; FakeServer server; QVERIFY(server.isListening());
        QString key;
        {
            DanmakuLibrary library(root.path()); library.setServers({server.base()});
            QSignalSpy ready(&library, &DanmakuLibrary::sourceReady);
            library.searchAnime("测试 中文"); QTRY_VERIFY(!library.busy());
            QCOMPARE(library.animes().size(), 1);
            QVERIFY(server.targets.first().startsWith("/prefix/api/v2/search/anime?keyword="));
            QCOMPARE(QUrl(QString::fromLatin1(server.base().toLatin1() + server.targets.first())).query(QUrl::FullyDecoded).contains("测试 中文"), true);
            library.selectAnime("42"); QTRY_VERIFY(!library.busy()); QCOMPARE(library.episodes().size(), 1);
            library.selectEpisode("42001"); library.downloadEpisode(); QTRY_COMPARE(ready.count(), 1);
            auto result = library.takeReadyResult(); QVERIFY(result); QCOMPARE(result->items.size(), std::size_t(2));
            QVERIFY(server.targets.last().contains("withRelated=true&chConvert=0"));
            for (const auto& request : server.requests) {
                QVERIFY(!request.toLower().contains("authorization:")); QVERIFY(!request.toLower().contains("x-signature:"));
            }
            QTRY_VERIFY(!library.scanning()); QCOMPARE(library.cachedEntries().size(), 1); QVERIFY(library.selectedCached());
            key = library.cachedEntries()[0].toMap()["key"].toString();
            const auto count = server.targets.size();
            library.downloadEpisode(); QTRY_COMPARE(ready.count(), 2); library.takeReadyResult(); QCOMPARE(server.targets.size(), count);
            library.downloadEpisode(true); QTRY_COMPARE(ready.count(), 3); library.takeReadyResult(); QCOMPARE(server.targets.size(), count + 1);
            server.response = [](QByteArray) { return Response{503, "unavailable"}; };
            library.downloadEpisode(true); QTRY_VERIFY(!library.busy()); QCOMPARE(ready.count(), 3);
            QVERIFY(readCachedDanmaku(root.filePath("danmaku"), key).error.isEmpty());
        }
        server.close();
        DanmakuLibrary offline(root.path());
        QSignalSpy ready(&offline, &DanmakuLibrary::sourceReady);
        QTRY_VERIFY(!offline.scanning()); QCOMPARE(offline.cachedEntries().size(), 1);
        offline.loadCached(key); QTRY_COMPARE(ready.count(), 1); QVERIFY(offline.takeReadyResult());
        offline.removeCached(key); QTRY_VERIFY(!offline.busy()); QTRY_VERIFY(!offline.scanning()); QVERIFY(offline.cachedEntries().isEmpty());
    }
    void failuresCancellationAndStaleResponses() {
        QTemporaryDir root; FakeServer server;
        DanmakuLibrary library(root.path(), nullptr, 200);
        library.setServers({server.base()});
        for (const auto status : {401, 403, 500}) {
            server.response = [status](QByteArray) { return Response{status, "error"}; };
            library.searchAnime("测试"); QTRY_VERIFY(!library.busy());
            QVERIFY(status == 500 ? library.error().contains("HTTP 500") : library.error().contains("认证"));
        }
        server.response = [](QByteArray) { return Response{200, "{"}; };
        library.searchAnime("测试"); QTRY_VERIFY(!library.busy()); QVERIFY(!library.error().isEmpty());
        server.response = [](QByteArray) { return Response{200, R"({"animes":[]})"}; };
        library.searchAnime("测试"); QTRY_VERIFY(!library.busy()); QVERIFY(!library.error().isEmpty()); QVERIFY(library.animes().isEmpty());
        server.response = [](QByteArray) { return Response{200, "", -1}; };
        library.searchAnime("测试"); QTRY_VERIFY(!library.busy()); QVERIFY(library.error().contains("超时"));
        server.response = [](QByteArray) { return Response{200, R"({"animes":[{"animeId":1,"animeTitle":"旧结果"}]})", 100}; };
        const auto count = server.targets.size(); library.searchAnime("旧查询"); QTRY_VERIFY(server.targets.size() > count);
        library.cancel(); QTest::qWait(150); QVERIFY(library.animes().isEmpty()); QVERIFY(!library.busy());
        library.searchAnime("旧查询"); QTest::qWait(20);
        server.response = [](QByteArray) { return Response{200, R"({"animes":[{"animeId":2,"animeTitle":"新结果"}]})"}; };
        library.searchAnime("新查询"); QTRY_VERIFY(!library.busy()); QTest::qWait(120);
        QCOMPARE(library.animes().first().toMap()["title"].toString(), QString("新结果"));
        library.searchAnime("查询"); library.setServers({"https://different.test"}); QTest::qWait(80);
        QVERIFY(library.animes().isEmpty()); QVERIFY(!library.busy());
        library.setServers({server.base()});
        server.response = [](QByteArray) { return Response{200, "", 0, "Content-Length: 67108865\r\n"}; };
        library.searchAnime("超限"); QTRY_VERIFY(!library.busy()); QVERIFY(library.error().contains("64 MiB"));
    }
    void alternateEpisodeFormatAndCacheWriteFailure() {
        QTemporaryDir root; FakeServer server, unused;
        DanmakuLibrary library(root.path()); library.setServers({server.base(), unused.base()});
        library.searchAnime("测试"); QTRY_VERIFY(!library.busy());
        server.response = [](QByteArray target) {
            if (target.contains("/bangumi/")) return Response{200, R"({"episodes":[{"episodeId":"42001","episodeTitle":"第1话"}]})"};
            return Response{200, commentBody()};
        };
        library.selectAnime("42"); QTRY_VERIFY(!library.busy()); QCOMPARE(library.episodes().size(), 1);
        library.selectEpisode("42001");
        writeBytes(root.filePath("danmaku"), "directory is blocked by a file");
        QSignalSpy ready(&library, &DanmakuLibrary::sourceReady);
        library.downloadEpisode(true); QTRY_COMPARE(ready.count(), 1);
        auto result = library.takeReadyResult(); QVERIFY(result); QCOMPARE(result->items.size(), std::size_t(2));
        QVERIFY(!result->warning.isEmpty()); QVERIFY(library.status().contains("缓存失败"));
        QVERIFY(unused.targets.isEmpty());
        // Changing animation while a delayed episode list arrives discards the old list.
        server.response = [](QByteArray) { return Response{200, R"({"episodes":[{"episodeId":999,"episodeTitle":"旧集数"}]})", 100}; };
        library.selectAnime("42"); QTest::qWait(20); library.cancel(); QTest::qWait(150);
        QVERIFY(library.episodes().isEmpty()); QVERIFY(library.selectedEpisode().isEmpty());
    }
    void controllerSourceLifecycle() {
        QTemporaryDir root; FakeServer server;
        AppController controller(root.path(), nullptr, [](const QString&) { return true; }, root.filePath("cache"));
        auto* library = qobject_cast<DanmakuLibrary*>(controller.library()); QVERIFY(library);
        QVERIFY(qobject_cast<SettingsStore*>(controller.settings())->setValue("danmakuServers", QStringList{server.base()}));
        library->searchAnime("测试"); QTRY_VERIFY(!library->busy()); library->selectAnime("42"); QTRY_VERIFY(!library->busy()); library->selectEpisode("42001");
        QSignalSpy loaded(&controller, &AppController::loadCompleted);
        library->downloadEpisode(); QTRY_COMPARE(loaded.count(), 1);
        QCOMPARE(controller.total(), 2); QVERIFY(!controller.running()); QVERIFY(controller.sourceTitle().contains("测试动画"));
        DanmakuItem renderer;
        QQuickWindow overlay;
        controller.attach(&renderer, &overlay);
        controller.start(true);
        QVERIFY(controller.running()); QVERIFY(controller.playing());
        QVERIFY(controller.filePath().isEmpty()); QVERIFY(controller.settings()->property("values").toMap()["lastFile"].toString().isEmpty());
        QTRY_VERIFY(!library->scanning());
        const auto key = library->cachedEntries().first().toMap()["key"].toString();
        library->removeCached(key); QTRY_VERIFY(!library->busy()); QCOMPARE(controller.total(), 2);
        server.response = [](QByteArray) { return Response{503, "failed"}; };
        library->downloadEpisode(true); QTRY_VERIFY(!library->busy()); QCOMPARE(controller.total(), 2);
        QVERIFY(controller.running()); QVERIFY(controller.playing());
        server.response = [](QByteArray) { return Response{200, commentBody("新弹幕"), 100}; };
        const auto count = server.targets.size(); library->downloadEpisode(true); QTRY_VERIFY(server.targets.size() > count);
        controller.stop(); QTest::qWait(160); QCOMPARE(controller.total(), 0); QCOMPARE(loaded.count(), 1);
        const auto xml = root.filePath("local.xml"); writeBytes(xml, R"(<i><d p="0,1,25,16777215">local</d></i>)");
        library->downloadEpisode(true); QTest::qWait(20); controller.loadFile(xml);
        QTRY_VERIFY(!controller.loading()); QTest::qWait(120); QCOMPARE(controller.total(), 1); QCOMPARE(controller.filePath(), xml);
        // A queued cached result must also lose to an explicit stop.
        library->downloadEpisode(true); QTRY_VERIFY(!library->busy()); QCOMPARE(controller.total(), 2);
        QTRY_VERIFY(!library->scanning()); const auto cachedKey = library->cachedEntries().first().toMap()["key"].toString();
        library->loadCached(cachedKey); controller.stop(); QTest::qWait(100); QCOMPARE(controller.total(), 0);
        // Switching from XML preparation must also notify QML that XML loading ended.
        controller.loadFile(xml); QVERIFY(controller.loading());
        QSignalSpy state(&controller, &AppController::stateChanged);
        library->loadCached(cachedKey); QVERIFY(!controller.loading()); QVERIFY(state.count() > 0);
        QTRY_VERIFY(!library->busy()); QCOMPARE(controller.total(), 2); QVERIFY(!controller.running());
        auto* quitting = new DanmakuLibrary(root.filePath("quit")); quitting->setServers({server.base()});
        quitting->searchAnime("退出"); delete quitting; QTest::qWait(120);
    }
    void sourcePagesSmoke() {
        QTemporaryDir root;
        auto result = parseOnlineDanmaku(commentBody()); result.metadata = metadata();
        const auto cacheRoot = root.filePath("cache");
        QVERIFY(saveCachedDanmaku(QDir(cacheRoot).filePath("danmaku"), result).isEmpty());
        const auto damagedKey = danmakuCacheKey("https://damaged.test", 1);
        writeBytes(QDir(cacheRoot).filePath("danmaku/" + damagedKey + ".json"), "broken");
        for (const auto& source : {"online", "cache"}) {
            QProcess process;
            process.start(QDir(QCoreApplication::applicationDirPath()).filePath("DanmaX.exe"),
                {"--smoke-test", "--seconds", "3", "--source-tab", source, "--data-dir", root.filePath(QString(source) + "-data"),
                 "--cache-dir", cacheRoot, "--window-width", "520", "--theme", source == QString("cache") ? "dark" : "light"});
            QVERIFY(process.waitForStarted()); QVERIFY(process.waitForFinished(15000));
            QCOMPARE(process.exitStatus(), QProcess::NormalExit);
            QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
        }
    }
};
QTEST_MAIN(OnlineDanmakuTests)
#include "OnlineDanmakuTests.moc"
