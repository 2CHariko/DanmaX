#include "../fixtures/PlayerExecutableFixture.h"
#include "../fixtures/MediaWorkflowProbe.h"
#include <limits>
#include "infrastructure/VlcHttpBackend.h"
#include "infrastructure/VlcProtocol.h"
#include "infrastructure/ExternalPlayerDraft.h"
#include "application/AppController.h"
#include "platform/windows/VlcLauncher.h"
#include <QTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>
#include <QSaveFile>
#include <QDir>
#include <QQuickWindow>
#include <QScopeGuard>
#include <atomic>

namespace {
struct Fixture {
    QTemporaryDir directory;
    QTcpServer server;
    QString executable, video, uri, item = "1", state = "playing", operation;
    double position = 12.25, rate = 1;
    int opens{}, requests{}, controls{}, launches{}, delay{}, size{};
    bool living = true, foreign = false;
    Fixture() {
        executable = write("vlc.exe", playerExecutableFixture());
        write("lua/intf/http.luac", "fixture"); write("lua/intf/modules/httprequests.luac", "fixture");
        video = write(QStringLiteral("中文 [% & #] - 01.mp4"), "fixture");
        uri = QUrl::fromLocalFile(video).toString(QUrl::FullyEncoded);
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            while (server.hasPendingConnections()) {
                auto* socket = server.nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer] {
                    buffer->append(socket->readAll()); if (!buffer->contains("\r\n\r\n")) return;
                    const auto line = buffer->split('\n').first().split(' '); if (line.size() < 2) return;
                    const auto url = QUrl::fromEncoded("http://127.0.0.1" + line[1]); const QUrlQuery query(url);
                    const auto get = [&](const QString& name) { return query.queryItemValue(name, QUrl::FullyDecoded); };
                    const auto command = get("command"); const auto previous = item; bool ok = true; ++requests;
                    if (command == "open" && operation != get("operation")) {
                        operation = get("operation"); uri = get("uri"); item = QString::number(item.toInt() + 1); ++opens;
                    } else if (command == "pause" || command == "resume" || command == "seek") {
                        ++controls; ok = get("expected_uri") == uri && get("expected_item") == item;
                        if (ok && command == "seek") position = get("seconds").toDouble();
                        else if (ok) state = command == "pause" ? "paused" : "playing";
                    }
                    QJsonObject obj{{"protocol", 1}, {"request", get("request")}, {"ok", ok}, {"found", true},
                        {"previous_item", previous}, {"uri", uri}, {"item", item}, {"position", position}, {"duration", 120.5}, {"rate", rate}, {"state", state}, {"canSeek", true}};
                    const auto body = size ? QByteArray(size, 'x') : QJsonDocument(obj).toJson(QJsonDocument::Compact);
                    const auto send = [socket, body] {
                        socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: "
                            + QByteArray::number(body.size()) + "\r\n\r\n" + body); socket->disconnectFromHost();
                    };
                    buffer->clear(); if (delay) QTimer::singleShot(delay, socket, send); else send();
                });
            }
        });
    }
    QString write(const QString& name, const QByteArray& bytes) {
        const auto path = directory.filePath(name); QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path); if (!file.open(QIODevice::WriteOnly)) return {}; file.write(bytes); return file.fileName();
    }
    VlcHttpBackend::Hooks hooks() {
        return {[this](const QString&, const QStringList& args, const QMap<QString, QString>&) {
            ++launches; for (const auto& arg : args) if (arg.startsWith("--http-port=")) {
                if (!server.listen(QHostAddress::LocalHost, arg.mid(12).toUShort())) return ManagedPlaybackOpen{{}, "bind failed"};
            }
            return ManagedPlaybackOpen{"danmax-vlc:123:456", {}};
        }, [this](const QString&, const QString&) { return living; }, [this](quint16, const QString&) {
            return foreign ? PlayerProcessIdentity::Listener::Foreign : PlayerProcessIdentity::Listener::Owned;
        }, [] { return QVariantList{}; }};
    }
};
MediaSample latest(QSignalSpy& spy) { return qvariant_cast<MediaSample>(spy.last().first()); }
void closeOwned(const QString& session) {
    if (!PlayerProcessIdentity::alive(session)) return;
    const auto pid = session.section(':', 1, 1).toUInt();
    HANDLE handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
    if (handle) { TerminateProcess(handle, 0); WaitForSingleObject(handle, 3000); CloseHandle(handle); }
}
}
class VlcPlaybackTests final : public QObject {
    Q_OBJECT
private slots:
    void processDiscoveryAndInactiveCleanup() {
        Fixture f; std::atomic<int> phase{0}; auto hooks = f.hooks();
        const QVariantMap first{{"id", "danmax-vlc:123:456"}, {"label", "VLC A"}, {"connectable", false}};
        const QVariantMap second{{"id", "danmax-vlc:987:654"}, {"label", "VLC B"}, {"connectable", false}};
        hooks.discover = [&phase, first, second] {
            const auto value = phase.load();
            return value == 0 ? QVariantList{first, second} : value == 1 ? QVariantList{second} : QVariantList{};
        };
        VlcHttpBackend backend(f.directory.filePath("cache"), nullptr, std::move(hooks));
        QSignalSpy sessions(&backend, &MediaPlaybackBackend::sessionsChanged);
        QTRY_VERIFY(!sessions.isEmpty()); QCOMPARE(sessions.last()[0].toList().size(), 2);
        QVERIFY(!sessions.last()[0].toList().first().toMap().value("connectable").toBool());
        phase = 1; QTRY_COMPARE_WITH_TIMEOUT(sessions.last()[0].toList().size(), 1, 3000);
        const auto opened = backend.open(f.executable, f.video); QVERIFY(opened.error.isEmpty());
        QCOMPARE(sessions.last()[0].toList().size(), 2);
        QVERIFY(sessions.last()[0].toList().first().toMap().value("connectable").toBool());
        // The managed instance is removed even without selecting/starting its polling timer.
        f.living = false; phase = 2;
        QTRY_VERIFY_WITH_TIMEOUT(sessions.last()[0].toList().isEmpty(), 3000);
        const auto count = sessions.size(); QTest::qWait(1100); QCOMPARE(sessions.size(), count);
    }
    void unavailableProcessDoesNotReplaceBinding() {
        QTemporaryDir directory; AppController controller(directory.path());
        auto* monitor = controller.findChild<MediaMonitor*>(); QVERIFY(monitor);
        const auto selected = qobject_cast<SettingsStore*>(controller.settings())->values().value("targetSession");
        monitor->sessionsChanged({QVariantMap{{"id", "danmax-vlc:123:456"}, {"connectable", false}, {"reason", "Open through DanmaX"}}});
        controller.selectSession("danmax-vlc:123:456");
        QCOMPARE(qobject_cast<SettingsStore*>(controller.settings())->values().value("targetSession"), selected);
        QCOMPARE(controller.error(), QString("Open through DanmaX"));
    }
    void protocolValidationAndPrecision() {
        const QString uri = QUrl::fromLocalFile("D:/媒体/中文 [% & #].mp4").toString(QUrl::FullyEncoded);
        QJsonObject obj{{"protocol", 1}, {"request", "7"}, {"ok", true}, {"found", true}, {"uri", uri}, {"item", "2"},
            {"position", 12.345678}, {"duration", 1420.086}, {"rate", 1.50150156}, {"state", "paused"}};
        const auto decode = [&] { return VlcProtocol::decode(QJsonDocument(obj).toJson(), "7", "danmax-vlc:1:2", 4, 20); };
        auto result = decode(); QVERIFY(result); QCOMPARE(result->sample.position, 12.345678); QCOMPARE(result->sample.generation, 4);
        QCOMPARE(result->sample.playback, MediaSample::Playback::Paused); QVERIFY(!result->sample.playing);
        const auto token = result->sample.sessionToken; obj["item"] = "3"; QVERIFY(decode()->sample.sessionToken != token);
        for (const auto* field : {"request", "protocol", "uri", "position", "duration", "rate", "state", "found"}) {
            const auto old = obj.value(field); obj[field] = "invalid"; QVERIFY(!decode()); obj[field] = old;
        }
        obj["position"] = -1; QVERIFY(!decode()); obj["position"] = 10000; QVERIFY(!decode());
        QVERIFY(!VlcProtocol::decode(QByteArray(70000, 'x'), "7", "id", 1, 0));
    }
    void presetValidationAndPersistence() {
        Fixture f; QVERIFY2(VlcLauncher::validate(f.executable).isEmpty(), qPrintable(VlcLauncher::validate(f.executable)));
        ExternalPlayerStore store(f.directory.path()); ExternalPlayerDraft draft(&store);
        draft.edit("vlc"); draft.enterExecutable(f.executable); draft.setDefault("vlc"); QVERIFY(draft.save());
        ExternalPlayerStore loaded(f.directory.path()); QCOMPARE(loaded.current().adapter, QString("vlc"));
        QVERIFY(loaded.selected().value("synchronize").toBool());
        QVERIFY(QFile::remove(f.directory.filePath("lua/intf/http.luac"))); QVERIFY(!VlcLauncher::validate(f.executable).isEmpty());
    }
    void managedOpenAndControlIsolation() {
        Fixture f; VlcHttpBackend backend(f.directory.filePath("cache"), nullptr, f.hooks());
        QSignalSpy samples(&backend, &MediaPlaybackBackend::sampleReceived), controls(&backend, &MediaPlaybackBackend::playbackControlFinished);
        const auto opened = backend.open(f.executable, f.video); QVERIFY2(opened.error.isEmpty(), qPrintable(opened.error));
        backend.select(opened.sessionId, 2); QTRY_VERIFY_WITH_TIMEOUT(!samples.isEmpty() && latest(samples).found, 4000);
        QCOMPARE(latest(samples).mediaUri, QUrl::fromLocalFile(f.video).toString(QUrl::FullyEncoded)); QCOMPARE(f.opens, 1);
        backend.requestPlayback(latest(samples), false, 1); QTRY_COMPARE(controls.size(), 1); QVERIFY(controls.last()[1].toBool());
        QTRY_COMPARE(latest(samples).playback, MediaSample::Playback::Paused);
        QSignalSpy seeks(&backend, &MediaPlaybackBackend::seekFinished);
        backend.requestSeek(latest(samples), 31.25, 4); QTRY_COMPARE(seeks.size(), 1); QVERIFY(seeks.first()[1].toBool());
        QTRY_COMPARE(latest(samples).position, 31.25);
        backend.requestSeek(latest(samples), -1, 5); QTRY_COMPARE(seeks.size(), 2); QVERIFY(!seeks.last()[1].toBool());
        auto invalidSeek = latest(samples); invalidSeek.duration = std::numeric_limits<double>::quiet_NaN();
        backend.requestSeek(invalidSeek, 10, 6); QCOMPARE(seeks.size(), 3); QVERIFY(!seeks.last()[1].toBool());
        invalidSeek = latest(samples); invalidSeek.observedAt = std::numeric_limits<double>::quiet_NaN();
        backend.requestSeek(invalidSeek, 10, 7); QCOMPARE(seeks.size(), 4); QVERIFY(!seeks.last()[1].toBool());
        const auto old = latest(samples); f.item = "99"; f.uri = QUrl::fromLocalFile(f.write("other.mp4", "video")).toString(QUrl::FullyEncoded);
        backend.requestPlayback(old, true, 2); QTRY_COMPARE(controls.size(), 2); QVERIFY(!controls.last()[1].toBool());
        QCOMPARE(f.state, QString("paused")); // Server rejects a content change between observation and command.
        backend.requestPlayback(old, false, 3); QTRY_COMPARE(controls.size(), 3); QVERIFY(!controls.last()[1].toBool());
        f.position = 80; QTRY_VERIFY(latest(samples).found && latest(samples).position == 80);
        const auto reopened = backend.open(f.executable, f.video); QCOMPARE(reopened.sessionId, opened.sessionId); QCOMPARE(f.launches, 1);
        backend.select(reopened.sessionId, 3); QTRY_VERIFY(latest(samples).found && latest(samples).generation == 3);
        QCOMPARE(f.opens, 2); f.living = false; QTRY_VERIFY(!latest(samples).found);
    }
    void openingWaitsForBackendAndChecksFullUri() {
        Fixture f; VideoOpenService service;
        ExternalPlayerProfile profile; profile.id = profile.adapter = "vlc"; profile.name = "VLC"; profile.executable = f.executable;
        service.configureProfile(profile);
        QSignalSpy requested(&service, &VideoOpenService::managedOpenRequested), opening(&service, &VideoOpenService::opening), confirmed(&service, &VideoOpenService::confirmed);
        service.open({QUrl::fromLocalFile(f.video)}); QVERIFY(service.pending()); QCOMPARE(requested.size(), 1); QCOMPARE(opening.size(), 0);
        QCOMPARE(requested.first()[1].toString(), f.executable); QCOMPARE(requested.first()[2].toString(), QFileInfo(f.video).canonicalFilePath());
        service.backendPrepared("danmax-vlc:123:456"); QCOMPARE(opening.size(), 1);
        MediaSample sample; sample.id = "danmax-vlc:123:456"; sample.found = true; sample.duration = 120;
        sample.playback = MediaSample::Playback::Playing; sample.observedAt = MediaMonitor::monotonicSeconds();
        sample.title = QFileInfo(f.video).fileName(); sample.mediaUri = QUrl::fromLocalFile(f.directory.filePath("other/" + sample.title)).toString();
        service.observe(sample); QCOMPARE(confirmed.size(), 0);
        sample.mediaUri = QUrl::fromLocalFile(f.video).toString(); service.observe(sample); QCOMPARE(confirmed.size(), 1);
        QVERIFY(service.active()); QVERIFY(!service.needsConfirmation());
    }
    void staleResponsesForeignPortsAndSizeLimit() {
        Fixture f; f.foreign = true;
        VlcHttpBackend backend(f.directory.filePath("cache"), nullptr, f.hooks());
        QSignalSpy samples(&backend, &MediaPlaybackBackend::sampleReceived), errors(&backend, &MediaPlaybackBackend::errorOccurred);
        auto opened = backend.open(f.executable, f.video); QVERIFY(opened.error.isEmpty()); backend.select(opened.sessionId, 1);
        QTRY_VERIFY(!errors.isEmpty()); QCOMPARE(f.requests, 0);
        f.foreign = false; f.delay = 300; backend.select(opened.sessionId, 2);
        QTRY_VERIFY(f.requests > 0); backend.select({}, 3); samples.clear(); QTest::qWait(450); QCOMPARE(samples.size(), 0);
        f.delay = 0; f.size = 70000; backend.open(f.executable, f.video); backend.select(opened.sessionId, 4);
        QTRY_VERIFY(!samples.isEmpty()); QVERIFY(!latest(samples).found);
        f.size = 0; backend.open(f.executable, f.video); backend.select(opened.sessionId, 5);
        QTRY_VERIFY(latest(samples).found && latest(samples).generation == 5);
    }
    void realVlcVideoAndAutomaticDanmaku() {
        const auto video = qEnvironmentVariable("DANMAKU_REAL_VLC_VIDEO");
        if (video.isEmpty()) QSKIP("Set DANMAKU_REAL_VLC_VIDEO for the real VLC integration test.");
        const auto evidence = qEnvironmentVariable("DANMAKU_REAL_VLC_ARTIFACTS"); QVERIFY(!evidence.isEmpty()); QDir().mkpath(evidence);
        AppController controller(QDir(evidence).filePath("data"), nullptr, [](const QString&) { return true; }, QDir(evidence).filePath("cache"));
        auto* store = qobject_cast<ExternalPlayerStore*>(controller.players()); ExternalPlayerDraft draft(store);
        draft.edit("vlc"); draft.discover(); QVERIFY2(draft.issue().isEmpty(), qPrintable(draft.issue())); draft.setDefault("vlc"); QVERIFY(draft.save());
        auto* settings = qobject_cast<SettingsStore*>(controller.settings()); settings->setValue("foregroundOnly", false);
        auto* service = qobject_cast<VideoOpenService*>(controller.videoOpen());
        auto* automatic = qobject_cast<AutomaticDanmaku*>(qobject_cast<MediaSession*>(controller.media())->automatic());
        auto* monitor = controller.findChild<MediaMonitor*>(); QVERIFY(monitor);
        QString ordinarySession;
        auto ordinaryCleanup = qScopeGuard([&] { closeOwned(ordinarySession); });
        QMap<QString, QString> environment;
        for (const auto* key : {"APPDATA", "LOCALAPPDATA", "TEMP", "TMP"}) {
            const auto folder = QDir(evidence).filePath("ordinary-runtime/" + QString::fromLatin1(key));
            QVERIFY(QDir().mkpath(folder)); environment.insert(QString::fromLatin1(key), QDir::toNativeSeparators(folder));
        }
        qint64 ordinaryPid = 0;
        const auto executable = store->current().executable;
        const auto launchError = ExternalPlayerProcess::launch(executable,
            {"--ignore-config", "--no-one-instance", "--no-one-instance-when-started-from-file", "--intf=dummy",
             "--no-media-library", "--no-metadata-network-access"}, &ordinaryPid, environment);
        QVERIFY2(launchError.isEmpty(), qPrintable(launchError));
        ordinarySession = PlayerProcessIdentity::session(ordinaryPid, executable); QVERIFY(!ordinarySession.isEmpty());
        const auto ordinaryListed = [&] {
            for (const auto& entry : controller.sessions()) if (entry.toMap().value("id").toString() == ordinarySession)
                return !entry.toMap().value("connectable").toBool();
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(ordinaryListed(), 5000);
        const auto originalBinding = settings->values().value("targetSession"); controller.selectSession(ordinarySession);
        QCOMPARE(settings->values().value("targetSession"), originalBinding); QVERIFY(!controller.error().isEmpty());
        closeOwned(ordinarySession); QTRY_VERIFY_WITH_TIMEOUT(!ordinaryListed(), 5000);
        MediaSample sample; connect(monitor, &MediaMonitor::sampleReceived, &controller, [&](MediaSample value) { sample = value; });
        QString session; auto cleanup = qScopeGuard([&] { controller.stop(); closeOwned(session); closeOwned(service->sessionId()); });
        QQuickWindow overlay; overlay.resize(1280, 720); overlay.setColor(Qt::transparent);
        overlay.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
        DanmakuItem renderer; renderer.setParentItem(overlay.contentItem()); renderer.setSize(QSizeF(1280, 720));
        connect(&overlay, &QQuickWindow::widthChanged, &renderer, [&] { renderer.setWidth(overlay.width()); });
        connect(&overlay, &QQuickWindow::heightChanged, &renderer, [&] { renderer.setHeight(overlay.height()); });
        controller.attach(&renderer, &overlay); renderer.setSize(overlay.size());
        connect(&controller, &AppController::stateChanged, &overlay, [&] { overlay.setVisible(controller.overlayVisible()); });
        service->open({QUrl::fromLocalFile(video)}); session = service->sessionId();
        QElapsedTimer elapsed; elapsed.start();
        while (!controller.running() && !automatic->playbackRecovery()->active() && controller.error().isEmpty() && elapsed.elapsed() < 60000) QTest::qWait(100);
        if (controller.running()) QTest::qWait(2000);
        bool ink = false;
        if (overlay.isVisible()) {
            const auto image = overlay.grabWindow().convertToFormat(QImage::Format_ARGB32); image.save(QDir(evidence).filePath("overlay.png"));
            for (int y = 0; y < image.height() && !ink; ++y) for (int x = 0; x < image.width(); ++x) if (qAlpha(image.pixel(x,y)) && qRed(image.pixel(x,y)) + qGreen(image.pixel(x,y)) + qBlue(image.pixel(x,y)) > 60) { ink = true; break; }
        }
        QJsonObject report{{"session", session}, {"running", controller.running()}, {"error", controller.error()}, {"status", automatic->status()},
            {"recovery", automatic->playbackRecovery()->reason()}, {"count", controller.total()}, {"uri", sample.mediaUri}, {"position", controller.position()},
            {"duration", controller.duration()}, {"overlayVisible", controller.overlayVisible()}, {"renderedInk", ink}, {"elapsedMs", elapsed.elapsed()}};
        QSaveFile output(QDir(evidence).filePath("result.json")); QVERIFY(output.open(QIODevice::WriteOnly)); output.write(QJsonDocument(report).toJson()); QVERIFY(output.commit());
        QVERIFY2(controller.running(), qPrintable(automatic->status() + "; " + controller.error())); QVERIFY(controller.targetSessionConnected()); QVERIFY(controller.total() > 0); QVERIFY(ink);
        const auto discovered = PlayerProcessIdentity::discoverVlc();
        QVERIFY(std::any_of(discovered.begin(), discovered.end(), [&](const QVariant& entry) { return entry.toMap().value("id").toString() == session; }));
        int ownedEntries = 0;
        for (const auto& entry : controller.sessions()) if (entry.toMap().value("id").toString() == session) {
            ++ownedEntries; QVERIFY(entry.toMap().value("connectable").toBool());
        }
        QCOMPARE(ownedEntries, 1);
        QSignalSpy controls(monitor, &MediaMonitor::playbackControlFinished);
        monitor->requestPlayback(sample, false, 1001); QTRY_VERIFY_WITH_TIMEOUT(!controls.isEmpty(), 4000); QVERIFY2(controls.last()[1].toBool(), qPrintable(controls.last()[2].toString()));
        QTRY_VERIFY(!controller.playing()); const auto paused = controller.position(); QTest::qWait(350); QVERIFY(std::abs(controller.position() - paused) < 0.05);
        monitor->requestPlayback(sample, true, 1002); QTRY_COMPARE_WITH_TIMEOUT(controls.size(), 2, 4000); QVERIFY(controls.last()[1].toBool()); QTRY_VERIFY(controller.playing());
        QSignalSpy confirmed(service, &VideoOpenService::confirmed); service->open({QUrl::fromLocalFile(video)});
        QTRY_COMPARE_WITH_TIMEOUT(confirmed.size(), 1, 15000); QCOMPARE(service->sessionId(), session);
        QVERIFY(!service->needsConfirmation());
        verifyMediaWorkflow(controller, monitor, service, sample, video, evidence, session);
    }
};
QTEST_MAIN(VlcPlaybackTests)
#include "VlcPlaybackTests.moc"
