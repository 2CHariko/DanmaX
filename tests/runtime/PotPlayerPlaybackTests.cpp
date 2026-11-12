#include "platform/windows/PotPlayerBackend.h"
#include "platform/windows/PlayerProcessIdentity.h"
#include "platform/windows/MediaMonitor.h"
#include "application/AppController.h"
#include "infrastructure/ExternalPlayerStore.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QProcess>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QScopeGuard>
#include <windows.h>
#include <cmath>
#include <limits>

namespace {
HWND window(const QString& id) {
    struct Search { QString id; HWND result{}; } search{id};
    EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(data);
        wchar_t cls[64]{}; GetClassNameW(hwnd, cls, 64);
        if (QString::fromWCharArray(cls) == "PotPlayer64" && PlayerProcessIdentity::matchesWindow(quintptr(hwnd), search.id)) {
            search.result = hwnd; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search)); return search.result;
}
qint64 query(HWND hwnd, WPARAM command, LPARAM value = 0) {
    DWORD_PTR reply{};
    return SendMessageTimeoutW(hwnd, WM_USER, command, value, SMTO_ABORTIFHUNG | SMTO_BLOCK, 500, &reply)
        ? qint64(static_cast<INT_PTR>(reply)) : -999;
}
MediaSample latest(const QSignalSpy& spy) { return spy.empty() ? MediaSample{} : qvariant_cast<MediaSample>(spy.last()[0]); }
QString write(const QString& directory, const QString& name, const QByteArray& data) {
    QFile file(QDir(directory).filePath(name)); if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) return {};
    return file.fileName();
}
}
class PotPlayerPlaybackTests final : public QObject {
    Q_OBJECT
private slots:
    void timedOutNativeSeekCompletesWithoutDuplicateAndCoalesces() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto exe = directory.filePath("PotPlayerMini64.exe"); QVERIFY(QFile::copy(QString::fromUtf8(POTPLAYER_FIXTURE), exe));
        const auto video = write(directory.path(), "delayed.avi", "fixture");
        MediaMonitor monitor(nullptr, directory.filePath("cache")); SeekController seek(&monitor);
        QSignalSpy samples(&monitor, &MediaMonitor::sampleReceived), done(&seek, &SeekController::finished), receipts(&monitor, &MediaMonitor::seekDispatched);
        const auto opened = monitor.openManaged("potplayer", exe, video); QVERIFY(opened.error.isEmpty());
        monitor.select(opened.sessionId, true); QTRY_VERIFY(!samples.empty() && latest(samples).found);
        QCOMPARE(query(window(opened.sessionId), 0x5ff3, 700), 1);
        seek.requestSeek(latest(samples), 30, 1);
        QTRY_VERIFY_WITH_TIMEOUT(!done.empty(), 5000); QVERIFY(done.last()[1].toBool());
        QCOMPARE(qvariant_cast<SeekDispatch>(receipts.first()[0]).outcome, SeekDispatch::Outcome::Unknown);
        QCOMPARE(query(window(opened.sessionId), 0x5ff4), 1);
        seek.requestSeek(latest(samples), 60, 2); seek.requestSeek(latest(samples), 90, 3); seek.requestSeek(latest(samples), 95, 4);
        QTRY_VERIFY_WITH_TIMEOUT(done.last()[0].toULongLong() == 4 && done.last()[1].toBool(), 6000);
        QCOMPARE(query(window(opened.sessionId), 0x5ff4), 3); // 30, 60, 95; 90 was never sent.
        monitor.closeOwnedPlayer(opened.sessionId); QTRY_VERIFY(!PlayerProcessIdentity::alive(opened.sessionId));
    }
    void realLargeSeeksAndLatestTarget() {
        const auto executable = qEnvironmentVariable("DANMAKU_REAL_POTPLAYER");
        const auto video = QFileInfo(qEnvironmentVariable("DANMAKU_REAL_POTPLAYER_SEEK_VIDEO")).canonicalFilePath();
        if (executable.isEmpty() || video.isEmpty()) QSKIP("Explicit real large-seek video not supplied");
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto xml = write(directory.path(), "seek.xml", "<i><d p=\"0,1,25,16777215\">Seek transaction</d><d p=\"1000,1,25,16777215\">Later</d></i>");
        const auto data = directory.filePath("data"); QDir().mkpath(data);
        { RecentVideoStore recent(data); recent.opened(video); recent.checkpoint(video, 0, 1500, {{"kind", "xml"}, {"path", xml}}); }
        AppController app(data, nullptr, [](const QString&) { return true; }, directory.filePath("cache"));
        auto* settings = qobject_cast<SettingsStore*>(app.settings()); settings->setDanmakuServers({}); settings->setValue("foregroundOnly", false);
        QVERIFY(qobject_cast<ExternalPlayerStore*>(app.players())->setExecutable(executable));
        QQuickWindow overlay; overlay.resize(800, 600); DanmakuItem renderer; renderer.setParentItem(overlay.contentItem()); renderer.setSize({800, 600}); app.attach(&renderer, &overlay);
        auto* media = qobject_cast<MediaSession*>(app.media()); auto* now = qobject_cast<NowPlayingModel*>(app.nowPlaying());
        media->openVideo(video); QTRY_VERIFY2_WITH_TIMEOUT(now->canSeek() && now->count() == 2, qPrintable(media->error() + media->status()), 15000);
        const auto session = media->boundSession(); QJsonArray report;
        for (const double target : {120.0, 600.0, 1100.0, 407.0}) {
            QElapsedTimer elapsed; elapsed.start(); now->seek(target); QVERIFY(now->seekPending());
            QCOMPARE(now->seekPreviewPosition(), target);
            QTRY_VERIFY_WITH_TIMEOUT(!now->seekPending(), 11000);
            QVERIFY2(now->error().isEmpty(), qPrintable(now->error())); QVERIFY(media->active()); QCOMPARE(now->count(), 2);
            report.append(QJsonObject{{"target", target}, {"actual", now->position()}, {"status", now->status()}, {"elapsedMs", elapsed.elapsed()}});
        }
        now->seek(200); now->seek(700); now->seek(1000);
        QCOMPARE(now->seekPreviewPosition(), 1000.0);
        QTRY_VERIFY_WITH_TIMEOUT(!now->seekPending(), 16000); QVERIFY2(now->error().isEmpty(), qPrintable(now->error()));
        report.append(QJsonObject{{"latestTarget", 1000}, {"actual", now->position()}, {"status", now->status()}});
        const auto output = qEnvironmentVariable("DANMAKU_POTPLAYER_ARTIFACTS");
        if (!output.isEmpty()) { QDir().mkpath(output); QSaveFile file(QDir(output).filePath("large-seeks.json")); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(QJsonDocument(report).toJson()); QVERIFY(file.commit()); }
        now->endPlayback(); QTRY_VERIFY(!PlayerProcessIdentity::alive(session));
    }
    void playlistCaptionChangesKeepPlayback() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto exe = directory.filePath("PotPlayerMini64.exe"); QVERIFY(QFile::copy(QString::fromUtf8(POTPLAYER_FIXTURE), exe));
        const auto name = QStringLiteral("[KitaujiSub] Anime [01][WebRip][CHS_JP].mp4");
        const auto video = write(directory.path(), name, "fixture");
        PotPlayerBackend backend; const auto opened = backend.open(exe, video); QVERIFY(opened.error.isEmpty());
        backend.select(opened.sessionId, 1);
        QSignalSpy samples(&backend, &MediaPlaybackBackend::sampleReceived), errors(&backend, &MediaPlaybackBackend::errorOccurred);
        QTRY_VERIFY_WITH_TIMEOUT(latest(samples).found, 5000);
        const auto original = latest(samples);
        for (const auto& title : QStringList{name + " - PotPlayer", "[1/13] " + name,
                 "[12/13] " + name + " - PotPlayer", QStringLiteral("暂停"), name}) {
            samples.clear();
            QVERIFY(SetWindowTextW(window(opened.sessionId), reinterpret_cast<LPCWSTR>(title.utf16())));
            QTRY_VERIFY(samples.size() >= 3);
            QVERIFY2(latest(samples).found, qPrintable(title));
            QCOMPARE(latest(samples).title, name);
            QCOMPARE(latest(samples).mediaUri, original.mediaUri);
            QCOMPARE(latest(samples).sessionToken, original.sessionToken);
            QVERIFY(errors.empty());
        }
        // This backend has no verified current-file getter: even a filename-like
        // caption is only display metadata, never proof of a different file.
        QVERIFY(SetWindowTextW(window(opened.sessionId), L"[2/13] [KitaujiSub] Anime [02].mp4"));
        QCOMPARE(query(window(opened.sessionId), 0x5ff2, 125000), 1);
        QTRY_COMPARE(latest(samples).duration, 125.0); QVERIFY(latest(samples).found); QVERIFY(errors.empty());
        QCOMPARE(latest(samples).evidence, MediaSample::Evidence::LaunchTarget);
        QVERIFY(latest(samples).mediaUri.isEmpty()); QVERIFY(!latest(samples).launchUri.isEmpty());
        backend.closeOwned(); QTRY_VERIFY_WITH_TIMEOUT(!PlayerProcessIdentity::alive(opened.sessionId), 5000);
    }
    void nativeIsolationControlGenerationAndLifetime() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto exe = directory.filePath("PotPlayerMini64.exe"); QVERIFY(QFile::copy(QString::fromUtf8(POTPLAYER_FIXTURE), exe));
        const auto video = write(directory.path(), "same.avi", "fixture");
        PotPlayerBackend sentinel, owned;
        const auto a = sentinel.open(exe, video), b = owned.open(exe, video);
        QVERIFY2(a.error.isEmpty(), qPrintable(a.error)); QVERIFY2(b.error.isEmpty(), qPrintable(b.error)); QVERIFY(a.sessionId != b.sessionId);
        sentinel.select(a.sessionId, 1); owned.select(b.sessionId, 7);
        QSignalSpy samples(&owned, &MediaPlaybackBackend::sampleReceived), controls(&owned, &MediaPlaybackBackend::playbackControlFinished),
            seeks(&owned, &MediaPlaybackBackend::seekFinished), exited(&owned, &PotPlayerBackend::ownedExited);
        QTRY_VERIFY_WITH_TIMEOUT(latest(samples).found, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(window(a.sessionId) != nullptr, 5000);
        QCOMPARE(query(window(a.sessionId), 0x5007, 1), 0); query(window(a.sessionId), 0x5005, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(latest(samples).rate - 1.5) < 0.2, 5000);
        QCOMPARE(latest(samples).duration, 120.0); QVERIFY(latest(samples).canSeek);
        owned.requestPlayback(latest(samples), false, 1); owned.requestPlayback(latest(samples), false, 2);
        QTRY_COMPARE_WITH_TIMEOUT(controls.size(), 2, 3000); QVERIFY(controls[0][1].toBool()); QVERIFY(controls[1][1].toBool());
        QTRY_VERIFY(!latest(samples).playing);
        const auto paused = query(window(b.sessionId), 0x5004); QTest::qWait(250);
        QCOMPARE(query(window(b.sessionId), 0x5004), paused);
        owned.requestSeek(latest(samples), 30.25, 3); QTRY_COMPARE(seeks.size(), 1); QVERIFY(seeks[0][1].toBool());
        QTRY_VERIFY(std::abs(latest(samples).position - 30.25) < 0.05);
        QCOMPARE(query(window(a.sessionId), 0x5004), 10000); QCOMPARE(query(window(a.sessionId), 0x5006), 1);
        const auto stale = latest(samples); owned.select(b.sessionId, 8);
        owned.requestPlayback(stale, true, 4); QTRY_COMPARE(controls.size(), 3); QVERIFY(!controls[2][1].toBool());
        owned.requestSeek(stale, std::numeric_limits<double>::quiet_NaN(), 5); QCOMPARE(seeks.size(), 2); QVERIFY(!seeks[1][1].toBool());
        QTRY_VERIFY(latest(samples).generation == 8);
        auto old = latest(samples); old.observedAt -= 5;
        owned.requestPlayback(old, true, 6); QTRY_COMPARE(controls.size(), 4); QVERIFY(!controls[3][1].toBool());
        QCOMPARE(query(window(b.sessionId), 0x5006), 1);
        owned.requestPlayback(latest(samples), true, 7); QTRY_COMPARE(controls.size(), 5); QVERIFY(controls[4][1].toBool());
        QTRY_VERIFY(latest(samples).playing);
        const auto expected = latest(samples);
        QVERIFY(SetWindowTextW(window(b.sessionId), L"different.avi"));
        QTest::qWait(250); QVERIFY(latest(samples).found);
        owned.requestSeek(expected, 90, 8); QTRY_COMPARE(seeks.size(), 3); QVERIFY(seeks[2][1].toBool());
        owned.closeOwned(b.sessionId); QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, 5000);
        QVERIFY(!PlayerProcessIdentity::alive(b.sessionId)); QVERIFY(PlayerProcessIdentity::alive(a.sessionId));
        owned.requestPlayback(expected, true, 9); QTRY_COMPARE(controls.size(), 6); QVERIFY(!controls[5][1].toBool());
        sentinel.closeOwned(); QTRY_VERIFY_WITH_TIMEOUT(!PlayerProcessIdentity::alive(a.sessionId), 5000);
    }
    void hungMessagesDoNotBlockGuiAndDestructorClosesOwned() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto exe = directory.filePath("PotPlayerMini64.exe"); QVERIFY(QFile::copy(QString::fromUtf8(POTPLAYER_FIXTURE), exe));
        const auto video = write(directory.path(), "timeout.avi", "fixture");
        auto backend = std::make_unique<PotPlayerBackend>(); const auto opened = backend->open(exe, video); QVERIFY(opened.error.isEmpty());
        backend->select(opened.sessionId, 1); QSignalSpy samples(backend.get(), &MediaPlaybackBackend::sampleReceived);
        QTRY_VERIFY_WITH_TIMEOUT(latest(samples).found, 5000);
        int ticks{}; QTimer heartbeat; heartbeat.setInterval(20); connect(&heartbeat, &QTimer::timeout, this, [&] { ++ticks; }); heartbeat.start();
        QCOMPARE(query(window(opened.sessionId), 0x5ff0, 1), 1);
        QTest::qWait(500); QVERIFY(ticks >= 10); QTRY_VERIFY(!latest(samples).found);
        backend.reset(); QTRY_VERIFY_WITH_TIMEOUT(!PlayerProcessIdentity::alive(opened.sessionId), 5000);
    }
    void immediateDestructionClosesWindowCreatedAfterShutdown() {
        QTemporaryDir directory;
        const auto exe = directory.filePath("PotPlayerMini64.exe"); QVERIFY(QFile::copy(QString::fromUtf8(POTPLAYER_FIXTURE), exe));
        const auto video = write(directory.path(), "startup-delay.avi", "fixture");
        auto backend = std::make_unique<PotPlayerBackend>(); const auto opened = backend->open(exe, video);
        QVERIFY2(opened.error.isEmpty(), qPrintable(opened.error)); QVERIFY(!window(opened.sessionId));
        backend.reset(); QTRY_VERIFY_WITH_TIMEOUT(!PlayerProcessIdentity::alive(opened.sessionId), 5000);
    }
    void realPlaylistVideoKeepsDanmaku() {
        const auto executable = qEnvironmentVariable("DANMAKU_REAL_POTPLAYER");
        const auto video = QFileInfo(qEnvironmentVariable("DANMAKU_REAL_POTPLAYER_MP4")).canonicalFilePath();
        if (executable.isEmpty() || video.isEmpty()) QSKIP("Explicit real playlist MP4 not supplied");
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QByteArray comments("<i>");
        for (int n = 0; n < 40; ++n)
            comments += "<d p=\"" + QByteArray::number(n * 0.5) + ",1,25,16777215\">Playlist regression</d>";
        comments += "</i>";
        const auto xml = write(directory.path(), "selected.xml", comments);
        const auto data = directory.filePath("data"); QDir().mkpath(data);
        { RecentVideoStore recent(data); recent.opened(video); recent.checkpoint(video, 0, 1500, {{"kind", "xml"}, {"path", xml}}); }
        AppController app(data, nullptr, [](const QString&) { return true; }, directory.filePath("cache"));
        auto* settings = qobject_cast<SettingsStore*>(app.settings()); settings->setDanmakuServers({});
        settings->setValue("foregroundOnly", false);
        auto* players = qobject_cast<ExternalPlayerStore*>(app.players()); QVERIFY(players->setExecutable(executable));
        QQuickWindow overlay; overlay.resize(800, 600); DanmakuItem renderer; renderer.setParentItem(overlay.contentItem());
        renderer.setSize({800, 600}); app.attach(&renderer, &overlay);
        connect(&app, &AppController::stateChanged, &overlay, [&] { overlay.setVisible(app.overlayVisible()); });
        auto* media = qobject_cast<MediaSession*>(app.media()); auto* now = qobject_cast<NowPlayingModel*>(app.nowPlaying());
        media->openVideo(video, MediaSession::FromStart);
        QTRY_VERIFY2_WITH_TIMEOUT(app.running() && now->canPlay() && now->count() == 40,
                                 qPrintable(media->error() + media->status()), 15000);
        const auto session = media->boundSession();
        QTRY_VERIFY_WITH_TIMEOUT(renderer.snapshotCount() > 0, 5000);
        QTest::qWait(2500);
        wchar_t title[2048]{}; GetWindowTextW(window(session), title, 2048);
        qInfo() << "Real playlist caption:" << QString::fromWCharArray(title);
        QVERIFY(media->active()); QVERIFY(now->canSelectSource()); QVERIFY(now->canPlay()); QCOMPARE(now->count(), 40);
        now->togglePlayback(); QTRY_VERIFY_WITH_TIMEOUT(!now->busy() && !now->playing(), 5000);
        now->seek(6); QTRY_VERIFY_WITH_TIMEOUT(!now->busy(), 6000);
        QVERIFY2(now->error().isEmpty(), qPrintable(now->error()));
        if (std::abs(now->position() - 6) > 1) QVERIFY(now->status().contains(QStringLiteral("实际定位")));
        QVERIFY(app.running()); QCOMPARE(now->count(), 40);
        app.stop(); QTRY_VERIFY_WITH_TIMEOUT(!PlayerProcessIdentity::alive(session), 5000);
    }
    void realManagedWorkflow() {
        const auto executable = qEnvironmentVariable("DANMAKU_REAL_POTPLAYER");
        const auto video = QFileInfo(qEnvironmentVariable("DANMAKU_REAL_POTPLAYER_VIDEO")).canonicalFilePath();
        if (executable.isEmpty() || video.isEmpty()) QSKIP("Explicit real PotPlayer/video not supplied");
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto xml = write(directory.path(), "selected.xml", "<i><d p=\"0,1,25,16777215\">Managed PotPlayer</d><d p=\"13,1,25,16777215\">Seek</d></i>");
        const auto data = directory.filePath("data"); QDir().mkpath(data);
        { RecentVideoStore recent(data); recent.opened(video); recent.checkpoint(video, 0, 60, {{"kind", "xml"}, {"path", xml}}); }
        // The sentinel is also created by this test. No user process is attached.
        PotPlayerBackend sentinel; const auto other = sentinel.open(executable, video); QVERIFY2(other.error.isEmpty(), qPrintable(other.error));
        const auto cleanup = qScopeGuard([&] { sentinel.closeOwned(); });
        QTRY_VERIFY_WITH_TIMEOUT(window(other.sessionId), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(query(window(other.sessionId), 0x5002), 60000, 10000);
        query(window(other.sessionId), 0x5007, 1);
        QTRY_COMPARE_WITH_TIMEOUT(query(window(other.sessionId), 0x5006), 1, 5000);
        query(window(other.sessionId), 0x5005, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(std::abs(query(window(other.sessionId), 0x5004) - 10000) <= 200, 5000);
        qint64 reference = query(window(other.sessionId), 0x5004);
        int stable = 0; QElapsedTimer settling; settling.start();
        while (stable < 3 && settling.elapsed() < 5000) {
            QTest::qWait(100); const auto position = query(window(other.sessionId), 0x5004);
            stable = position == reference ? stable + 1 : 0; reference = position;
        }
        QVERIFY(stable >= 3);
        AppController app(data, nullptr, [](const QString&) { return true; }, directory.filePath("cache"));
        auto* settings = qobject_cast<SettingsStore*>(app.settings()); settings->setDanmakuServers({});
        auto* players = qobject_cast<ExternalPlayerStore*>(app.players());
        const auto selection = players->saveDraft(players->profiles(), "potplayer", players->revision());
        QVERIFY2(selection.value("ok").toBool(), qPrintable(selection.value("error").toString()));
        QVERIFY2(players->setExecutable(executable), qPrintable(players->error()));
        QQuickWindow overlay; overlay.resize(800, 600); DanmakuItem renderer; renderer.setParentItem(overlay.contentItem());
        renderer.setSize({800, 600}); app.attach(&renderer, &overlay);
        auto* media = qobject_cast<MediaSession*>(app.media()); auto* now = qobject_cast<NowPlayingModel*>(app.nowPlaying());
        media->openVideo(video); QTRY_VERIFY2_WITH_TIMEOUT(app.running() && now->canPlay(), qPrintable(media->error() + media->status()), 15000);
        const auto first = media->boundSession(); QVERIFY(first.startsWith("danmax-potplayer:")); QVERIFY(first != other.sessionId);
        now->togglePlayback(); QTRY_VERIFY_WITH_TIMEOUT(!now->busy() && !now->playing(), 5000);
        QVERIFY(now->canSeek()); now->seek(12.5); QTRY_VERIFY_WITH_TIMEOUT(!now->busy() && std::abs(now->position() - 12.5) <= 0.25, 5000);
        media->flush();
        media->openVideo(video, MediaSession::Continue);
        QTRY_VERIFY2_WITH_TIMEOUT(app.running() && !media->restoring() && !media->restoreFailed() && media->position() >= 12, qPrintable(media->error()), 15000);
        QVERIFY(first != media->boundSession()); QTRY_VERIFY_WITH_TIMEOUT(!PlayerProcessIdentity::alive(first), 5000);
        const auto second = media->boundSession();
        now->openStandalone(directory.filePath("missing.xml")); QTRY_VERIFY(!now->openingStandalone());
        QVERIFY(media->active()); QVERIFY(PlayerProcessIdentity::alive(second));
        now->openStandalone(xml); QTRY_VERIFY(now->standalone()); QTRY_VERIFY_WITH_TIMEOUT(!PlayerProcessIdentity::alive(second), 5000);
        QVERIFY(app.manualMode()); now->togglePlayback(); now->seek(2); QCOMPARE(now->position(), 2.0);
        QCOMPARE(query(window(other.sessionId), 0x5006), 1);
        media->openVideo(video, MediaSession::Continue);
        QTRY_VERIFY_WITH_TIMEOUT(app.running() && !media->restoring() && !media->restoreFailed() && media->position() >= 12, 15000);
        QVERIFY(!now->standalone()); const auto stopped = media->boundSession();
        app.stop(); QTRY_VERIFY_WITH_TIMEOUT(!PlayerProcessIdentity::alive(stopped), 5000);
        QVERIFY(!app.running()); QVERIFY(!overlay.isVisible()); QVERIFY(!now->active()); QVERIFY(!app.targetSessionConnected());
        QCOMPARE(query(window(other.sessionId), 0x5006), 1);
        const auto stopDrift = query(window(other.sessionId), 0x5004) - reference;
        qInfo() << "Paused sentinel drift after task stop (ms):" << stopDrift;
        QVERIFY2(std::abs(stopDrift) <= 200, qPrintable(QString("Sentinel moved %1 ms (one 5fps frame allowed)").arg(stopDrift)));
        media->openVideo(video, MediaSession::FromStart);
        QTRY_VERIFY_WITH_TIMEOUT(app.running() && !media->restoring() && media->position() < 5, 15000);
        const auto third = media->boundSession(); QVERIFY(PostMessageW(window(third), WM_CLOSE, 0, 0));
        QTRY_VERIFY_WITH_TIMEOUT(!app.running() && !now->active() && !overlay.isVisible(), 5000);
        QVERIFY(!PlayerProcessIdentity::alive(third)); QVERIFY(PlayerProcessIdentity::alive(other.sessionId));
        QCOMPARE(query(window(other.sessionId), 0x5006), 1);
        const auto closeDrift = query(window(other.sessionId), 0x5004) - reference;
        qInfo() << "Paused sentinel drift after player close (ms):" << closeDrift;
        QVERIFY2(std::abs(closeDrift) <= 200, qPrintable(QString("Sentinel moved %1 ms (one 5fps frame allowed)").arg(closeDrift)));
        sentinel.closeOwned(); QTRY_VERIFY_WITH_TIMEOUT(!PlayerProcessIdentity::alive(other.sessionId), 5000);
        const auto output = qEnvironmentVariable("DANMAKU_POTPLAYER_ARTIFACTS");
        if (!output.isEmpty()) {
            QDir().mkpath(output); QSaveFile result(QDir(output).filePath("managed-workflow.json")); QVERIFY(result.open(QIODevice::WriteOnly));
            result.write(QJsonDocument(QJsonObject{{"pauseAndSeek", true}, {"continueWatching", true}, {"fromStart", true},
                {"replacementClosesPrevious", true}, {"toolbarStopClosesOwned", true}, {"playerCloseStopsOverlay", true},
                {"sameFileSentinelUnchanged", true}, {"failedIndependentXmlKeepsVideo", true}, {"standaloneHandoffClosesOwned", true},
                {"independentToVideoResume", true}, {"stopSession", stopped}, {"sentinelToleranceMs", 200}, {"sentinelStopDriftMs", stopDrift},
                {"sentinelCloseDriftMs", closeDrift}, {"firstSession", first}, {"secondSession", second}, {"thirdSession", third}}).toJson());
            QVERIFY(result.commit());
        }
    }
    void refusedCloseIsReportedWithoutTerminatingProcess() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto exe = directory.filePath("PotPlayerMini64.exe"); QVERIFY(QFile::copy(QString::fromUtf8(POTPLAYER_FIXTURE), exe));
        const auto video = write(directory.path(), "refused.avi", "fixture");
        PotPlayerBackend backend; const auto opened = backend.open(exe, video); QVERIFY(opened.error.isEmpty());
        const auto cleanup = qScopeGuard([&] {
            const auto hwnd = window(opened.sessionId); if (hwnd) { query(hwnd, 0x5ff1, 0); PostMessageW(hwnd, WM_CLOSE, 0, 0); }
        });
        backend.select(opened.sessionId, 1); QSignalSpy samples(&backend, &MediaPlaybackBackend::sampleReceived),
            failed(&backend, &PotPlayerBackend::closeFailed), exited(&backend, &PotPlayerBackend::ownedExited);
        QTRY_VERIFY_WITH_TIMEOUT(latest(samples).found, 5000);
        QCOMPARE(query(window(opened.sessionId), 0x5ff1, 1), 1); backend.closeOwned();
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 7000);
        QVERIFY(PlayerProcessIdentity::alive(opened.sessionId)); QVERIFY(exited.empty());
        query(window(opened.sessionId), 0x5ff1, 0); QVERIFY(PostMessageW(window(opened.sessionId), WM_CLOSE, 0, 0));
        QTRY_COMPARE_WITH_TIMEOUT(exited.size(), 1, 5000); QVERIFY(!PlayerProcessIdentity::alive(opened.sessionId));
    }
};
QTEST_MAIN(PotPlayerPlaybackTests)
#include "PotPlayerPlaybackTests.moc"
