#include "application/AutomaticDanmaku.h"
#include "application/AppController.h"
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QQuickWindow>
#include <functional>
#include <limits>

namespace {
class StubParser final : public IMediaTitleParser {
    QString name_, title_; double episode_;
public:
    StubParser(QString name, QString title, double episode) : name_(name), title_(title), episode_(episode) {}
    QString id() const override { return name_; }
    QList<MediaParseCandidate> parse(const MediaParseInput&) const override {
        return {{{title_, canonicalAnimeTitle(title_), episode_, true}, name_, {}, {"test evidence"}}};
    }
};
struct Response { QByteArray body; int delay{}, status{200}; };
class Server : public QTcpServer {
  public:
    QList<QByteArray> requests;
    bool ambiguous{}, alias{};
    int commentDelay{};
    std::function<Response(QByteArray)> response;
    Server() {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto bytes = std::make_shared<QByteArray>();
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, bytes] {
                    bytes->append(socket->readAll());
                    if (!bytes->contains("\r\n\r\n")) return;
                    disconnect(socket, &QTcpSocket::readyRead, this, nullptr);
                    const auto target = bytes->split(' ').value(1); requests.append(target);
                    const auto result = response ? response(target) : standard(target);
                    QTimer::singleShot(result.delay, socket, [guard = QPointer<QTcpSocket>(socket), result] {
                        if (!guard || guard->state() != QAbstractSocket::ConnectedState) return;
                        guard->write("HTTP/1.1 " + QByteArray::number(result.status) + " OK\r\nContent-Length: " +
                            QByteArray::number(result.body.size()) + "\r\nConnection: close\r\n\r\n" + result.body);
                        guard->disconnectFromHost();
                    });
                });
            }
        });
    }
    QString base() const { return QString("http://127.0.0.1:%1").arg(serverPort()); }
    int count(const char* path) const {
        int count = 0; for (const auto& request : requests) if (request.contains(path)) ++count; return count;
    }
    Response standard(const QByteArray& target) const {
        if (target.contains("search/anime")) {
            QJsonArray animes{QJsonObject{{"animeId", 42}, {"animeTitle", alias ? "Canonical" : "Test"}}};
            if (ambiguous) animes.append(QJsonObject{{"animeId", 43}, {"animeTitle", "Test"}});
            return {QJsonDocument(QJsonObject{{"animes", animes}}).toJson()};
        }
        if (target.contains("/bangumi/")) {
            QJsonArray episodes;
            for (int n = 1; n <= 5; ++n)
                episodes.append(QJsonObject{{"episodeId", 42000 + n}, {"episodeTitle", QString("第%1话").arg(n)}, {"episodeNumber", n}});
            return {QJsonDocument(QJsonObject{{"episodes", episodes}}).toJson()};
        }
        return {R"({"comments":[{"p":"0,1,16777215,user","m":"zero"},{"p":"10,1,16777215,user","m":"ten"},{"p":"90,1,16777215,user","m":"ninety"}]})", commentDelay};
    }
};
MediaSample media(QString title = "Test - 01.mkv") {
    MediaSample sample;
    sample.id = "Test.Player"; sample.title = title; sample.identity = title + "\n100";
    sample.position = 12; sample.duration = 100; sample.found = true;
    sample.playback = MediaSample::Playback::Playing;
    sample.observedAt = MediaMonitor::monotonicSeconds();
    return sample;
}
void keepAlive(QTimer& timer, AutomaticDanmaku& automatic, MediaSample& sample) {
    QObject::connect(&timer, &QTimer::timeout, &automatic, [&automatic, &sample] {
        sample.observedAt = MediaMonitor::monotonicSeconds(); automatic.observe(sample);
    });
    timer.start(20);
    automatic.observe(sample);
}
OnlineDanmakuResult cached(QString title, int episode, QString server = "https://offline.test") {
    auto result = parseOnlineDanmaku(R"({"comments":[{"p":"0,1,16777215,u","m":"cached"}]})");
    result.metadata = {{"animeId", "42"}, {"animeTitle", title}, {"episodeId", QString::number(42000 + episode)},
        {"episodeTitle", QString("第%1话").arg(episode)}, {"episodeNumber", episode}, {"server", server},
        {"downloadedAt", "2026-10-16T00:00:00.000Z"}};
    return result;
}
}

class AutomaticDanmakuTests final : public QObject {
    Q_OBJECT
private slots:
    void selectedPlayerLossHidesAndCleansOverlay() {
        QTemporaryDir directory;
        QFile fixture(directory.filePath("comments.xml")); QVERIFY(fixture.open(QIODevice::WriteOnly));
        // Reconnection recreates an asynchronous window surface. Comments at the
        // hidden instant are intentionally suppressed; verify the next visible
        // comment while providing fresh samples, as a real backend does.
        fixture.write("<i><d p=\"0,1,25,16777215\">first</d><d p=\"11,1,25,16777215\">next</d></i>"); fixture.close();
        AppController controller(directory.path(), nullptr, [](const QString&) { return true; }, directory.filePath("cache"));
        auto* monitor = controller.findChild<MediaMonitor*>(); QVERIFY(monitor);
        QObject::disconnect(monitor, &MediaMonitor::sampleReceived, &controller, nullptr);
        auto* settings = qobject_cast<SettingsStore*>(controller.settings());
        settings->setValue("foregroundOnly", false);
        controller.setRenderTestSession("Test.Player");
        QQuickWindow overlay; overlay.resize(800, 600);
        DanmakuItem renderer; renderer.setParentItem(overlay.contentItem()); renderer.setSize(QSizeF(800, 600));
        controller.attach(&renderer, &overlay);
        connect(&controller, &AppController::stateChanged, &overlay, [&] { overlay.setVisible(controller.overlayVisible()); });
        QSignalSpy loaded(&controller, &AppController::loadCompleted);
        controller.loadFile(fixture.fileName()); QTRY_COMPARE(loaded.size(), 1);
        controller.start(false); QVERIFY(controller.running()); QVERIFY(!controller.overlayVisible());
        auto sample = media(); sample.position = 0;
        const auto inject = [&] {
            sample.observedAt = MediaMonitor::monotonicSeconds(); sample.generation = monitor->generation();
            return QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
        };
        QVERIFY(inject()); QTRY_VERIFY(controller.overlayVisible());
        QVERIFY(controller.toolbarPresentationAllowed()); QTRY_VERIFY(renderer.snapshotCount() > 0);
        sample.playback = MediaSample::Playback::Paused; sample.position = 0.2; QVERIFY(inject());
        QVERIFY(controller.overlayVisible()); QVERIFY(!controller.playing());
        sample.id = "Other.Player"; sample.found = false; QVERIFY(inject());
        QVERIFY(controller.overlayVisible()); // Another app cannot hide the selected player's layer.
        sample.id = "Test.Player"; sample.found = true; sample.playback = MediaSample::Playback::Closed;
        QVERIFY(inject()); QVERIFY(!controller.overlayVisible()); QVERIFY(!controller.toolbarPresentationAllowed());
        QVERIFY(controller.running()); QCOMPARE(controller.total(), 2);
        QTRY_COMPARE_WITH_TIMEOUT(renderer.snapshotCount(), 0, 2500);
        QVERIFY(!settings->values().contains("targetSession"));
        sample.playback = MediaSample::Playback::Playing; sample.position = 10; QVERIFY(inject());
        QVERIFY(controller.overlayVisible()); QVERIFY(controller.playing()); QCOMPARE(controller.position(), 10.0);
        QElapsedTimer playbackTime; playbackTime.start();
        QTimer heartbeat; connect(&heartbeat, &QTimer::timeout, &controller, [&] {
            if (sample.playback == MediaSample::Playback::Playing) sample.position = 10 + playbackTime.elapsed() / 1000.0;
            inject();
        }); heartbeat.start(30);
        QTRY_VERIFY(renderer.snapshotCount() > 0);
        // Brief session loss hides immediately but reconnect cancels delayed cleanup.
        heartbeat.stop(); sample.found = false; QVERIFY(inject()); QVERIFY(!controller.overlayVisible());
        sample.found = true; sample.playback = MediaSample::Playback::Paused; QVERIFY(inject()); heartbeat.start();
        QTest::qWait(1700); QVERIFY(controller.overlayVisible()); QVERIFY(controller.targetSessionConnected());
        heartbeat.stop(); QTRY_VERIFY_WITH_TIMEOUT(!controller.overlayVisible(), 4000);
        QVERIFY(!controller.toolbarPresentationAllowed()); QTRY_COMPARE_WITH_TIMEOUT(renderer.snapshotCount(), 0, 4000);
        controller.stop(); controller.setManualMode(true); controller.start(true);
        QVERIFY(controller.overlayVisible()); sample.found = false; QVERIFY(inject()); QVERIFY(controller.overlayVisible());
        controller.stop();
    }
    void injectedParsersAndConflictArbitration() {
        for (int conflict = 0; conflict < 3; ++conflict) {
            std::vector<std::unique_ptr<IMediaTitleParser>> parsers;
            parsers.push_back(std::make_unique<StubParser>("extension-a", "Title", 1));
            parsers.push_back(std::make_unique<StubParser>("extension-b", conflict == 0 ? "Other title" : "Title", conflict == 1 ? 2 : 1));
            const auto result = HybridMediaTitleParser(std::move(parsers)).parse({"File - 01.mkv"});
            QCOMPARE(result.candidates.size(), 2);
            QCOMPARE(result.state, conflict < 2 ? MediaParseResult::State::NeedsConfirmation : MediaParseResult::State::Resolved);
            QCOMPARE(result.selected.valid(), conflict == 2);
        }
        std::vector<std::unique_ptr<IMediaTitleParser>> invalid;
        invalid.push_back(std::make_unique<StubParser>("invalid", "Title", std::numeric_limits<double>::infinity()));
        QVERIFY(!HybridMediaTitleParser(std::move(invalid)).parse({"File - 01"}).selected.valid());
    }
    void aliasesRequireEvidence_data() {
        QTest::addColumn<QString>("query"); QTest::addColumn<QString>("alias");
        QTest::newRow("different-season") << QString("A Really Long Series Season 2") << QString("A Really Long Series Season 22");
        QTest::newRow("different-word") << QString("A Really Long Wolf Tale") << QString("A Really Long Olf Tale");
    }
    void aliasesRequireEvidence() {
        QFETCH(QString, query); QFETCH(QString, alias);
        QTemporaryDir directory; Server server;
        server.response = [&](const QByteArray& path) -> Response {
            if (path.contains("search/anime")) return {R"({"animes":[{"animeId":42,"animeTitle":"Unrelated result"}]})"};
            if (path.contains("/bangumi/")) return {QJsonDocument(QJsonObject{{"bangumi", QJsonObject{
                {"titles", QJsonArray{QJsonObject{{"title", alias}}}},
                {"episodes", QJsonArray{QJsonObject{{"episodeId", 42001}, {"episodeTitle", "第1话"}, {"episodeNumber", 1}}}}
            }}}).toJson()};
            return server.standard(path);
        };
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 10);
        QSignalSpy ready(&automatic, &AutomaticDanmaku::sourceReady);
        automatic.configure(true, {server.base()});
        auto sample = media(query + " - 01.mkv"); QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QTRY_VERIFY(automatic.playbackRecovery()->active()); QCOMPARE(ready.size(), 0);
        QCOMPARE(server.count("/comment/"), 0);
    }
    void recoveryOwnershipAndCancellation() {
        PlaybackRecovery recovery;
        QSignalSpy commands(&recovery, &PlaybackRecovery::controlRequested);
        auto sample = media(); sample.sessionToken = 8; sample.generation = 4;
        recovery.begin(sample, "ambiguous");
        QVERIFY(recovery.active()); QCOMPARE(recovery.title(), sample.title);
        QCOMPARE(commands.size(), 1); QVERIFY(!commands[0][1].toBool());
        const auto pause = commands[0][2].toULongLong();
        recovery.finish(true); // Cancel before the pause acknowledgement arrives.
        recovery.controlFinished(pause, true, {});
        QCOMPARE(commands.size(), 1);
        sample.playback = MediaSample::Playback::Paused; recovery.observe(sample);
        QCOMPARE(commands.size(), 2); QVERIFY(commands[1][1].toBool());
        recovery.controlFinished(commands[1][2].toULongLong(), true, {});
        recovery.finish(true); recovery.observe(sample); QCOMPARE(commands.size(), 2);

        commands.clear(); recovery.begin(sample, "already paused"); recovery.finish(true);
        QCOMPARE(commands.size(), 0);
        sample.playback = MediaSample::Playback::Playing;
        recovery.begin(sample, "failure");
        recovery.controlFinished(commands.last()[2].toULongLong(), false, "unsupported");
        sample.playback = MediaSample::Playback::Paused; recovery.observe(sample); recovery.finish(true);
        QCOMPARE(commands.size(), 1); // A failed pause never owns a later user pause.
    }
    void recoveryInvalidationAndUserResume() {
        for (int change = 0; change < 6; ++change) {
            PlaybackRecovery recovery; QSignalSpy commands(&recovery, &PlaybackRecovery::controlRequested);
            auto sample = media(); sample.sessionToken = 3;
            recovery.begin(sample, "failure");
            const auto request = commands.last()[2].toULongLong();
            auto different = sample;
            if (change == 0) different.title += " next";
            if (change == 1) ++different.sessionToken;
            if (change == 2) ++different.generation;
            if (change == 3) different.id += "other";
            if (change == 4) different.found = false;
            if (change == 5) different.observedAt -= 10;
            recovery.observe(different); QVERIFY(!recovery.active());
            recovery.controlFinished(request, true, {}); recovery.finish(true);
            sample.playback = MediaSample::Playback::Paused; recovery.observe(sample);
            QCOMPARE(commands.size(), 1);
        }
        PlaybackRecovery recovery; QSignalSpy commands(&recovery, &PlaybackRecovery::controlRequested);
        auto sample = media(); recovery.begin(sample, "failure");
        sample.playback = MediaSample::Playback::Paused; recovery.observe(sample);
        recovery.controlFinished(commands[0][2].toULongLong(), true, {});
        sample.playback = MediaSample::Playback::Playing; recovery.observe(sample);
        sample.playback = MediaSample::Playback::Paused; recovery.observe(sample); recovery.finish(true);
        QCOMPARE(commands.size(), 1);
    }
    void recoveryTimeoutDoesNotOwnPlayback() {
        PlaybackRecovery recovery; QSignalSpy commands(&recovery, &PlaybackRecovery::controlRequested);
        auto sample = media(); recovery.begin(sample, "failure");
        const auto request = commands[0][2].toULongLong();
        QTRY_VERIFY_WITH_TIMEOUT(recovery.pauseStatus().contains(QStringLiteral("未能确认")), 3800);
        recovery.controlFinished(request, true, {});
        sample.observedAt = MediaMonitor::monotonicSeconds(); sample.playback = MediaSample::Playback::Paused;
        recovery.observe(sample); recovery.finish(true); QCOMPARE(commands.size(), 1);
    }
    void hybridReleaseParsingAndLegacyIdentity() {
        const QString raw = "[KitaujiSub] Kimi ga Shinu made Koi wo Shitai [01][WebRip][HEVC_AAC][CHS_JP].mp4";
        const auto report = identifyMediaTitle(raw);
        QCOMPARE(report.state, MediaParseResult::State::Resolved);
        QCOMPARE(report.selected.anime, QString("Kimi ga Shinu made Koi wo Shitai"));
        QCOMPARE(report.selected.episode, 1.0); QCOMPARE(report.candidates.size(), 2);
        const auto chinese = identifyMediaTitle(QStringLiteral("[字幕组][未来日记][01][BDRip][1080P].mkv"));
        QCOMPARE(chinese.selected.animeKey, canonicalAnimeTitle(QStringLiteral("未来日记")));
        QCOMPARE(chinese.selected.episode, 1.0);
        for (const auto& rawSpecial : {"Test_NCOP01.mkv", "Test_Menu_01.mkv", "Test - 01-02.mkv"}) {
            const auto special = identifyMediaTitle(rawSpecial);
            QVERIFY(!special.selected.valid()); QCOMPARE(special.state, MediaParseResult::State::NeedsConfirmation);
        }
        QTemporaryDir directory; MediaAssociationStore store(directory.path());
        const auto old = parseRuleMediaTitle(raw);
        QVERIFY(store.remember(old, cached("Canonical", 1).metadata).isEmpty());
        QVERIFY(!store.exact(report.selected).isEmpty());
        QVERIFY(store.rememberRaw(raw, cached("Canonical", 1).metadata, report.selected.animeKey).isEmpty());
        QVERIFY(!MediaAssociationStore(directory.path()).raw(raw).isEmpty());
        QVERIFY(store.forget(report.selected).isEmpty()); QVERIFY(store.raw(raw).isEmpty());
    }
    void cancelSkipsOnlyCurrentContent() {
        QTemporaryDir directory;
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 10);
        automatic.configure(true, {});
        QSignalSpy shown(automatic.playbackRecovery(), &PlaybackRecovery::showRequested);
        auto sample = media("unrecognized movie"); QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QTRY_COMPARE(shown.size(), 1); automatic.cancelRecovery();
        QTest::qWait(100); QCOMPARE(shown.size(), 1); QVERIFY(!automatic.playbackRecovery()->active());
        sample.title = "next unrecognized movie"; QTRY_COMPARE(shown.size(), 2);
    }
    void unrecognizedManualSelectionPersistsOffline() {
        QTemporaryDir directory; Server server;
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 10);
        automatic.configure(true, {server.base()});
        auto sample = media("UnrecognizedMovie.mkv"); sample.playback = MediaSample::Playback::Paused;
        QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QSignalSpy ready(&automatic, &AutomaticDanmaku::sourceReady);
        QTRY_VERIFY(automatic.playbackRecovery()->active());
        auto* library = qobject_cast<DanmakuLibrary*>(automatic.library());
        library->searchAnime("Test"); QTRY_COMPARE(library->animes().size(), 1);
        library->selectAnime("42"); QTRY_COMPARE(library->episodes().size(), 5);
        library->selectEpisode("42002"); library->downloadEpisode();
        QTRY_COMPARE(ready.size(), 1); QVERIFY(!automatic.playbackRecovery()->active());
        QCOMPARE(automatic.takeReadyResult()->metadata.value("episodeId").toString(), QString("42002"));
        heartbeat.stop(); automatic.configure(false, {});
        AutomaticDanmaku offline(directory.path(), directory.path(), nullptr, 10);
        QSignalSpy offlineReady(&offline, &AutomaticDanmaku::sourceReady);
        offline.configure(true, {}); sample.observedAt = MediaMonitor::monotonicSeconds(); offline.observe(sample);
        QTRY_COMPARE(offlineReady.size(), 1);
        QCOMPARE(offline.takeReadyResult()->metadata.value("episodeId").toString(), QString("42002"));
    }
    void verifiedAliasesDoNotPrompt() {
        QTemporaryDir directory; Server server;
        server.response = [&](const QByteArray& path) -> Response {
            if (path.contains("search/anime")) return {R"({"animes":[{"animeId":42,"animeTitle":"与你相恋到生命尽头"}]})"};
            if (path.contains("/bangumi/")) return {R"({"bangumi":{"titles":[{"title":"Kimi ga Shinu made Koi o Shitai"}],"episodes":[{"episodeId":42001,"episodeTitle":"第1话","episodeNumber":1}]}})"};
            return server.standard(path);
        };
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 10);
        QSignalSpy shown(automatic.playbackRecovery(), &PlaybackRecovery::showRequested);
        QSignalSpy ready(&automatic, &AutomaticDanmaku::sourceReady);
        automatic.configure(true, {server.base()});
        auto sample = media("[KitaujiSub] Kimi ga Shinu made Koi wo Shitai [01].mp4");
        QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QTRY_COMPARE(ready.size(), 1); QCOMPARE(shown.size(), 0);
    }
    void parseTitles_data() {
        QTest::addColumn<QString>("input"); QTest::addColumn<QString>("anime");
        QTest::addColumn<double>("episode"); QTest::addColumn<bool>("explicitEpisode");
        QTest::newRow("release") << QString("[NC-Raws] Frieren - 01 [1080p][HEVC].mkv") << QString("frieren") << 1.0 << true;
        QTest::newRow("season") << QString("Test S02E03.mp4") << canonicalAnimeTitle("Test 第二季") << 3.0 << true;
        QTest::newRow("first-season") << QString("Test S01E02.mkv") << QString("test") << 2.0 << true;
        QTest::newRow("chinese") << QStringLiteral("葬送的芙莉蓮 第 12 集") << canonicalAnimeTitle(QStringLiteral("葬送的芙莉莲")) << 12.0 << true;
        QTest::newRow("decimal") << QString("Test EP12.5v2") << QString("test") << 12.5 << true;
        QTest::newRow("path") << QString("D:\\Anime\\Test - 02.mkv") << QString("test") << 2.0 << true;
        QTest::newRow("bracket") << QString("Test [03] [720p]") << QString("test") << 3.0 << true;
        QTest::newRow("numeric-name") << QString("86 - 01") << QString("86") << 1.0 << true;
        QTest::newRow("bare-needs-confirmation") << QString("Test 12") << QString("test") << 12.0 << false;
        for (const auto& input : {"86", "Test Season 2", "Test - 01-02", "Test - 01 - 02", "Test EP01-EP02", "Test S01E01-E02", "Test 第1集-第2集", "Test SP01", "Test OP01", "Test S00E01", "Test 第0集", "Test", ""})
            QTest::newRow(input) << QString::fromUtf8(input) << QString() << 0.0 << false;
        QTest::newRow("oversize") << QString(1025, 'a') << QString() << 0.0 << false;
    }
    void parseTitles() {
        QFETCH(QString, input); QFETCH(QString, anime); QFETCH(double, episode); QFETCH(bool, explicitEpisode);
        const auto parsed = parseMediaTitle(input);
        QCOMPARE(parsed.animeKey, anime); QCOMPARE(parsed.episode, episode); QCOMPARE(parsed.explicitEpisode, explicitEpisode);
    }
    void normalizationAndAssociations() {
        QCOMPARE(canonicalAnimeTitle("Test Season 2"), canonicalAnimeTitle(QStringLiteral("Test 第二季")));
        QVERIFY(canonicalAnimeTitle("Test Season 2") != canonicalAnimeTitle("Test"));
        QCOMPARE(danmakuEpisodeNumber(QStringLiteral("第9话 冒险")), 9.0);
        QCOMPARE(danmakuEpisodeNumber("EP12.5"), 12.5);
        QCOMPARE(danmakuEpisodeNumber("Different title", 5), 5.0);
        QCOMPARE(danmakuEpisodeNumber(QStringLiteral("第1话-第2话"), 1), 0.0);
        QCOMPARE(danmakuEpisodeNumber("SP01", 1), 0.0);
        QCOMPARE(danmakuEpisodeNumber("Unnumbered memory 9"), 0.0);
        QCOMPARE(danmakuEpisodeNumber(QStringLiteral("剧集 9")), 0.0);
        QTemporaryDir directory;
        const auto title = parseMediaTitle("Alias - 01");
        const auto metadata = cached("Canonical", 1).metadata;
        {
            MediaAssociationStore store(directory.path());
            QVERIFY(store.remember(title, metadata).isEmpty());
        }
        MediaAssociationStore restored(directory.path());
        QCOMPARE(restored.exact(title).value("episodeId").toString(), QString("42001"));
        QCOMPARE(restored.series(title).value("animeTitle").toString(), QString("Canonical"));
        QVERIFY(restored.forget(title).isEmpty());
        QVERIFY(MediaAssociationStore(directory.path()).exact(title).isEmpty());
        auto excessive = metadata; excessive["episodeTitle"] = QString(513, 'x');
        QVERIFY(!restored.remember(title, excessive).isEmpty());
        auto extra = metadata; extra["unused"] = QString(1024 * 1024, 'x');
        QVERIFY(restored.remember(title, extra).isEmpty());
        QVERIFY(QFileInfo(directory.filePath("media-associations.json")).size() < 10000);
        QFile file(directory.filePath("media-associations.json"));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("{\"version\":99}"); file.close();
        MediaAssociationStore unknown(directory.path());
        QVERIFY(!unknown.remember(title, metadata).isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("{\"version\":99}"));
        file.close(); QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray malformed = R"({"version":1,"entries":{"bad":{}}})";
        file.write(malformed); file.close();
        MediaAssociationStore damaged(directory.path()); QVERIFY(!damaged.remember(title, metadata).isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), malformed);
    }
    void offlineCacheAndNoDuplicateLoads() {
        QTemporaryDir directory;
        QVERIFY(saveCachedDanmaku(directory.filePath("danmaku"), cached("Test", 1)).isEmpty());
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 10);
        QSignalSpy ready(&automatic, &AutomaticDanmaku::sourceReady);
        QSignalSpy invalidated(&automatic, &AutomaticDanmaku::contentInvalidated);
        QVERIFY(!qobject_cast<DanmakuLibrary*>(automatic.library())->scanning());
        automatic.configure(true, {});
        auto sample = media(); QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QTRY_COMPARE(ready.count(), 1);
        auto result = automatic.takeReadyResult(); QVERIFY(result); QCOMPARE(result->items.size(), size_t(1));
        sample.duration = 101; sample.identity = "metadata changed"; sample.title = "Test EP01 [720p]";
        QTest::qWait(150); QCOMPARE(ready.count(), 1); QCOMPARE(invalidated.count(), 1);
        sample.playback = MediaSample::Playback::Paused; sample.position = 44;
        QTest::qWait(80); QCOMPARE(ready.count(), 1); QCOMPARE(automatic.latestSample().position, 44.0);
        sample.found = false; QTRY_VERIFY(automatic.status().contains(QStringLiteral("等待")));
        sample.found = true; QTRY_COMPARE(ready.count(), 2);
    }
    void onlineUniqueAndAliasConfirmation() {
        QTemporaryDir directory; Server server; server.alias = true;
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 10);
        auto* library = qobject_cast<DanmakuLibrary*>(automatic.library());
        automatic.configure(true, {server.base()});
        QSignalSpy ready(&automatic, &AutomaticDanmaku::sourceReady);
        auto sample = media("Alias - 01"); QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QTRY_VERIFY(automatic.needsChoice()); QCOMPARE(ready.count(), 0);
        library->selectAnime("42"); QTRY_COMPARE(ready.count(), 1);
        auto result = automatic.takeReadyResult(); QVERIFY(result);
        QCOMPARE(result->metadata.value("episodeId").toString(), QString("42001"));
        sample.title = "Alias - 02"; QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(server.count("search/anime"), 1);
        QCOMPARE(server.count("/bangumi/"), 3); // Includes the initial alias verification detail request.
        automatic.takeReadyResult();
        heartbeat.stop(); automatic.configure(false, {});
        AutomaticDanmaku offline(directory.path(), directory.path(), nullptr, 10);
        offline.configure(true, {}); QSignalSpy offlineReady(&offline, &AutomaticDanmaku::sourceReady);
        sample.observedAt = MediaMonitor::monotonicSeconds(); offline.observe(sample);
        QTRY_COMPARE(offlineReady.count(), 1);
        QCOMPARE(offline.takeReadyResult()->metadata.value("episodeId").toString(), QString("42002"));
    }
    void ambiguityWeakTitlesAndCancellation() {
        QTemporaryDir directory; Server server; server.ambiguous = true;
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 10);
        automatic.configure(true, {server.base()});
        QSignalSpy ready(&automatic, &AutomaticDanmaku::sourceReady);
        auto sample = media(); QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QTRY_VERIFY(automatic.needsChoice()); QCOMPARE(ready.count(), 0);
        server.ambiguous = false; sample.title = "Test 02";
        QTRY_COMPARE(server.count("search/anime"), 2);
        QTRY_VERIFY(automatic.needsChoice()); QCOMPARE(ready.count(), 0);
        server.commentDelay = 350; sample.title = "Test - 03";
        QTRY_COMPARE(server.count("/comment/"), 1);
        automatic.suspend(); QTest::qWait(450); QCOMPARE(ready.count(), 0); QVERIFY(automatic.suspended());
        QSignalSpy invalidated(&automatic, &AutomaticDanmaku::contentInvalidated);
        automatic.forgetAssociation(); QVERIFY(automatic.suspended()); QCOMPARE(invalidated.count(), 0);
        sample.title = "Test - 04"; QTest::qWait(80); QCOMPARE(server.count("/comment/"), 1);
        automatic.retry(); QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(automatic.takeReadyResult()->metadata.value("episodeId").toString(), QString("42004"));
    }
    void rapidChangesAndLateResponses() {
        QTemporaryDir directory; Server server; server.commentDelay = 400;
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 10);
        automatic.configure(true, {server.base()});
        QSignalSpy ready(&automatic, &AutomaticDanmaku::sourceReady);
        auto sample = media(); QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QTRY_COMPARE(server.count("/comment/"), 1);
        sample.title = "Test - 02";
        QTRY_COMPARE(server.count("/comment/"), 2);
        sample.title = "Test - 03";
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(automatic.takeReadyResult()->metadata.value("episodeId").toString(), QString("42003"));
        QTest::qWait(500); QCOMPARE(ready.count(), 1);
        sample.title = "Test - 04"; QTRY_COMPARE(server.count("/comment/"), 4);
        automatic.configure(false, {server.base()}); QTest::qWait(500); QCOMPARE(ready.count(), 1);
    }
    void failureAndExplicitRetry() {
        QTemporaryDir directory; Server server;
        server.response = [](QByteArray) { return Response{"{}", 0, 503}; };
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 10);
        automatic.configure(true, {server.base()});
        auto sample = media(); QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QTRY_VERIFY(automatic.status().contains("503"));
        const auto requests = server.requests.size(); QTest::qWait(150); QCOMPARE(server.requests.size(), requests);
        server.response = {}; QSignalSpy ready(&automatic, &AutomaticDanmaku::sourceReady);
        automatic.retry(); QTRY_COMPARE(ready.count(), 1);
    }
    void conflictingCachesRequireConfirmation() {
        QTemporaryDir directory; Server server;
        auto first = cached("Test", 1, server.base());
        auto other = first; other.metadata["animeId"] = "99"; other.metadata["episodeId"] = "99001";
        QVERIFY(saveCachedDanmaku(directory.filePath("danmaku"), first).isEmpty());
        QVERIFY(saveCachedDanmaku(directory.filePath("danmaku"), other).isEmpty());
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 10);
        automatic.configure(true, {server.base()});
        QSignalSpy ready(&automatic, &AutomaticDanmaku::sourceReady);
        auto sample = media(); QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QTRY_VERIFY(automatic.needsChoice()); QCOMPARE(ready.count(), 0);
        QCOMPARE(server.count("/comment/"), 0);
    }
    void confirmedEpisodeCanBeDownloadedAgain() {
        QTemporaryDir directory; Server server; server.alias = true;
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 10);
        automatic.configure(true, {server.base()});
        QSignalSpy ready(&automatic, &AutomaticDanmaku::sourceReady);
        auto sample = media("Alias - 01"); QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QTRY_VERIFY(automatic.needsChoice());
        auto* library = qobject_cast<DanmakuLibrary*>(automatic.library()); library->selectAnime("42");
        QTRY_COMPARE(ready.count(), 1); automatic.takeReadyResult();
        const auto key = danmakuCacheKey(server.base(), 42001);
        QVERIFY(QFile::remove(directory.filePath("danmaku/" + key + ".json")));
        automatic.refreshCache(); QTRY_VERIFY(!library->scanning());
        automatic.retry(); QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(server.count("search/anime"), 1); QCOMPARE(server.count("/bangumi/"), 2);
        QCOMPARE(server.count("/comment/"), 2);
        QCOMPARE(automatic.takeReadyResult()->metadata.value("episodeId").toString(), QString("42001"));
    }
    void stableTitleDebounceAndExpiredCompletion() {
        QTemporaryDir directory; Server server;
        AutomaticDanmaku automatic(directory.path(), directory.path(), nullptr, 200);
        automatic.configure(true, {server.base()});
        QSignalSpy ready(&automatic, &AutomaticDanmaku::sourceReady);
        auto sample = media(); QTimer heartbeat; keepAlive(heartbeat, automatic, sample);
        QTest::qWait(40); sample.title = "Test - 02";
        QTest::qWait(40); sample.title = "Test - 03";
        QTRY_COMPARE(ready.count(), 1);
        QCOMPARE(server.count("search/anime"), 1);
        QCOMPARE(automatic.takeReadyResult()->metadata.value("episodeId").toString(), QString("42003"));
        server.commentDelay = 2800; sample.title = "Test - 04";
        QTRY_COMPARE(server.count("/comment/"), 2);
        heartbeat.stop(); QTest::qWait(3000); QCOMPARE(ready.count(), 1);
        QVERIFY(automatic.status().contains(QStringLiteral("等待")));
        server.commentDelay = 0; heartbeat.start();
        QTRY_COMPARE(ready.count(), 2);
        QCOMPARE(automatic.takeReadyResult()->metadata.value("episodeId").toString(), QString("42004"));
    }
};
QTEST_MAIN(AutomaticDanmakuTests)
#include "AutomaticDanmakuTests.moc"
