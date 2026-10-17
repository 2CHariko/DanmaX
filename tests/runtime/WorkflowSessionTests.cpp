#include "../fixtures/PlayerExecutableFixture.h"
#include "application/AppController.h"
#include "application/MediaSession.h"
#include "infrastructure/ExternalPlayerDraft.h"
#include "infrastructure/RecentVideoStore.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
QString write(const QString& directory, const QString& name, const QByteArray& content) {
    const auto path = QDir(directory).filePath(name); QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return {}; file.write(content); return path;
}
struct Fixture {
    QTemporaryDir directory;
    QString exe = write(directory.path(), "PotPlayerMini64.exe", playerExecutableFixture());
    QString video = write(directory.path(), "Test - 01.mp4", "video");
    QString xml = write(directory.path(), "first.xml", "<i><d p=\"0,1,25,16777215\">first</d><d p=\"50,1,25,16777215\">later</d></i>");
    QString second = write(directory.path(), "second.xml", "<i><d p=\"1,1,25,16777215\">other</d></i>");
    int launches{};
    VideoOpenService::Hooks hooks() { return {[this] { return exe; }, [](const QString&) { return QString(); },
        [this](const QString&, const QStringList&) { ++launches; return QString(); }}; }
    QString data() const { return directory.filePath("data"); }
    void seed(double position = 0) {
        RecentVideoStore store(data()); store.opened(video);
        store.checkpoint(video, position, 120, {{"kind", "xml"}, {"path", xml}});
    }
};
struct Harness {
    Fixture& f;
    AppController app;
    QQuickWindow window;
    DanmakuItem renderer;
    MediaSession* media;
    MediaMonitor* monitor;
    VideoOpenService* service;
    MediaSample sample;
    Harness(Fixture& fixture) : f(fixture), app(f.data(), nullptr, [](const QString&) { return true; }, f.directory.filePath("cache"), {}, {}, {}, f.hooks()) {
        media = qobject_cast<MediaSession*>(app.media()); monitor = app.findChild<MediaMonitor*>(); service = qobject_cast<VideoOpenService*>(app.videoOpen());
        QObject::disconnect(monitor, &MediaMonitor::sampleReceived, &app, nullptr);
        qobject_cast<SettingsStore*>(app.settings())->setDanmakuServers({});
        renderer.setParentItem(window.contentItem()); window.resize(800, 600); renderer.setSize(QSizeF(800, 600)); app.attach(&renderer, &window);
        sample.found = true; sample.playing = true; sample.playback = MediaSample::Playback::Playing;
        sample.duration = 120; sample.position = 12; sample.sessionToken = 101; sample.canSeek = true;
        sample.canPlay = sample.canPause = true;
    }
    void send() {
        QTest::qWait(2); sample.id = service->sessionId(); sample.generation = monitor->generation(); sample.observedAt = MediaMonitor::monotonicSeconds();
        sample.identity = sample.title; sample.mediaUri = QUrl::fromLocalFile(f.video).toString(QUrl::FullyEncoded);
        QMetaObject::invokeMethod(&app, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
    }
    void open(int intent = MediaSession::Normal) {
        media->openVideo(f.video, intent); sample.title = QFileInfo(f.video).fileName(); send();
    }
};
}
class WorkflowSessionTests final : public QObject {
    Q_OBJECT
private slots:
    void standalonePlaybackReplacementUnloadAndEnd() {
        Fixture f; Harness h(f); auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying());
        auto* standalone = qobject_cast<DanmakuSession*>(h.app.standalone()); QVERIFY(standalone);
        now->openStandalone(f.xml); QTRY_VERIFY(now->standalone()); QTRY_COMPARE(now->count(), 2);
        QVERIFY(now->active()); QVERIFY(now->playing()); QVERIFY(now->canPlay()); QVERIFY(now->canSeek());
        QVERIFY(!now->video()); QVERIFY(!h.media->active()); QVERIFY(!h.service->active());
        now->togglePlayback(); QVERIFY(!now->playing()); now->seek(6); QCOMPARE(now->position(), 6.0);
        QTest::qWait(80); QCOMPARE(now->position(), 6.0);
        now->replaceDanmaku(f.second); QTRY_COMPARE(now->count(), 1);
        QVERIFY(!now->playing()); QCOMPARE(now->position(), 6.0); QCOMPARE(now->duration(), 16.0);
        now->togglePlayback(); QVERIFY(now->playing());
        now->unloadDanmaku(); QVERIFY(now->active()); QCOMPARE(now->count(), 0); QVERIFY(!now->canPlay());
        QVERIFY(now->canSelectSource()); now->replaceDanmaku(f.xml); QTRY_COMPARE(now->count(), 2);
        now->endPlayback(); QVERIFY(!now->active()); QVERIFY(!h.app.running()); QCOMPARE(now->count(), 0);
        QVERIFY(!now->canSeek()); QVERIFY(!now->canSelectSource());
        now->openStandalone(f.xml); QTRY_VERIFY(now->standalone()); h.app.stop();
        QVERIFY(!now->active()); QVERIFY(!h.app.overlayVisible());
    }
    void independentPreparationPreservesVideoUntilCommit() {
        Fixture f; f.seed(); Harness h(f); h.open(); QTRY_COMPARE(h.media->total(), 2);
        auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying());
        const auto bound = h.service->sessionId(); const auto generation = h.monitor->generation();
        now->openStandalone(f.directory.filePath("missing.xml")); QTRY_VERIFY(!now->openingStandalone());
        QVERIFY(!now->error().isEmpty()); QVERIFY(h.media->active()); QVERIFY(h.app.running());
        QCOMPARE(h.media->total(), 2); QCOMPARE(h.service->sessionId(), bound); QCOMPARE(h.monitor->generation(), generation);
        h.media->loadFile(f.xml); QTRY_VERIFY(!h.media->loading()); QVERIFY(now->error().isEmpty());
        now->openStandalone(f.second); now->cancelStandaloneOpen(); QTest::qWait(80);
        QVERIFY(h.media->active()); QVERIFY(now->video()); QCOMPARE(now->count(), 2);
        now->openStandalone(f.second); now->unloadDanmaku(); QTest::qWait(60);
        QVERIFY(now->video()); QVERIFY(h.media->active()); QCOMPARE(now->count(), 0);
        h.media->loadFile(f.xml); QTRY_COMPARE(h.media->total(), 2);
        now->openStandalone(f.second); QTRY_VERIFY(now->standalone()); QCOMPARE(now->count(), 1);
        QVERIFY(!h.media->active()); QVERIFY(!h.service->active()); QVERIFY(h.app.manualMode());
        h.send(); h.monitor->ownedPlayerExited(bound); QVERIFY(now->standalone()); QVERIFY(now->playing());
    }
    void standalonePreparationCannotTakeOverLaterVideoOrEnd() {
        Fixture f; f.seed(); Harness h(f); auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying());
        disconnect(h.media, &MediaSession::seekRequested, h.monitor, &MediaMonitor::requestSeek);
        QSignalSpy seeks(h.media, &MediaSession::seekRequested);
        now->openStandalone(f.second); h.open(); QTRY_COMPARE(h.media->total(), 2); QTest::qWait(60);
        QVERIFY(now->video()); QVERIFY(!now->standalone());
        now->openStandalone(f.second); now->endPlayback(); QTest::qWait(60);
        QVERIFY(!now->active()); QVERIFY(!h.app.running());
        now->openStandalone(f.second); h.app.stop(); QTest::qWait(60);
        QVERIFY(!now->active()); QVERIFY(!h.app.running()); QVERIFY(!now->openingStandalone());
        now->openStandalone(f.second); QTRY_VERIFY(now->standalone()); h.open();
        if (h.media->restoring()) { QVERIFY(!seeks.isEmpty()); h.monitor->seekFinished(seeks.last()[2].toULongLong(), true, {}); h.send(); }
        QTRY_COMPARE(h.media->total(), 2); QVERIFY(!qobject_cast<DanmakuSession*>(h.app.standalone())->active());
        QVERIFY(now->video()); QVERIFY(!h.app.manualMode());
    }
    void standaloneCachedSourcesAreTaskBound() {
        Fixture f; Harness h(f); auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying());
        auto result = parseOnlineDanmaku(R"({"comments":[{"p":"0,1,16777215,u","m":"cached"}]})");
        result.metadata = {{"server", "https://offline.test"}, {"animeId", "42"}, {"episodeId", "42001"}, {"animeTitle", "Cached"}, {"episodeTitle", "Episode 1"}, {"downloadedAt", "2026-10-17T00:00:00.000Z"}};
        QVERIFY(saveCachedDanmaku(f.directory.filePath("cache/danmaku"), result).isEmpty());
        const auto key = danmakuCacheKey("https://offline.test", 42001);
        now->openStandalone(f.xml); QTRY_VERIFY(now->standalone()); now->togglePlayback(); now->seek(6);
        auto* task = qobject_cast<DanmakuSession*>(h.app.standalone());
        h.app.loadCachedDanmaku(key); QTRY_COMPARE(now->count(), 1); QCOMPARE(now->title(), QString("Cached"));
        QVERIFY(!now->playing()); QCOMPARE(now->position(), 6.0); QVERIFY(!h.media->active());
        task->loadFile(f.xml); QVERIFY(task->preparing()); task->loadCached(key); QVERIFY(!task->preparing());
        QTRY_COMPARE(now->count(), 1); QTest::qWait(60); QCOMPARE(now->count(), 1);
        task->loadCached(key); now->endPlayback(); QTest::qWait(80); QVERIFY(!now->active());
        now->openStandalone(f.xml); QTRY_VERIFY(now->standalone());
        task->loadCached(key); h.open(); QTest::qWait(100); QVERIFY(now->video()); QCOMPARE(now->count(), 0);
        QVERIFY(!task->active());
    }
    void recentPersistenceBoundsAndReplacement() {
        Fixture f; RecentVideoStore store(f.data()); store.opened(f.video);
        store.checkpoint(f.video, 30, 120, {{"kind", "xml"}, {"path", f.xml}});
        QCOMPARE(store.entries().size(), 1); QVERIFY(store.entries().first().toMap().value("canResume").toBool());
        { RecentVideoStore loaded(f.data()); QCOMPARE(loaded.lookup(f.video).value("position").toDouble(), 30.0); }
        store.opened(f.video); QCOMPARE(store.entries().size(), 1);
        store.checkpoint(f.video, -1, 120, {}); QCOMPARE(store.lookup(f.video).value("position").toDouble(), 30.0);
        write(f.directory.path(), "Test - 01.mp4", "different size");
        QCOMPARE(store.lookup(f.video).value("position").toDouble(), 0.0); QVERIFY(store.lookup(f.video).value("source").toMap().isEmpty());
        for (int i = 0; i < 102; ++i) store.opened(write(f.directory.path(), QString("video-%1.mp4").arg(i), "video"));
        QCOMPARE(store.entries().size(), 100); QCOMPARE(store.rowCount(), 100);
        auto path = store.entries().first().toMap().value("path").toString(); QVERIFY(QFile::remove(path)); store.refresh();
        QVERIFY(store.entries().first().toMap().value("missing").toBool()); store.remove(path); QCOMPARE(store.rowCount(), 99);
    }
    void corruptHistoryAndWriteFailurePreserveOriginal() {
        Fixture f; QDir().mkpath(f.data()); const auto path = write(f.data(), "recent-videos.json", "broken");
        RecentVideoStore store(f.data()); QVERIFY(!store.error().isEmpty()); store.opened(f.video);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("broken")); file.close();
        QVERIFY(QFile::remove(path)); QVERIFY(QDir().mkdir(path)); RecentVideoStore failed(f.data()); failed.opened(f.video); QVERIFY(!failed.error().isEmpty());
        QVERIFY(QFileInfo(path).isDir());
    }
    void resumeBoundariesAndUnchangedCheckpoint() {
        Fixture f; RecentVideoStore store(f.data()); store.opened(f.video);
        const auto resumable = [&](double position) {
            store.checkpoint(f.video, position, 120, {});
            return store.entries().first().toMap().value("canResume").toBool();
        };
        QVERIFY(!resumable(4.999)); QVERIFY(resumable(5)); QVERIFY(resumable(109.999));
        QVERIFY(!resumable(110)); QVERIFY(!resumable(120));
        QFile history(QDir(f.data()).filePath("recent-videos.json")); QVERIFY(history.open(QIODevice::ReadOnly));
        const auto before = history.readAll(); history.close();
        store.checkpoint(f.video, 120, 120, {});
        QVERIFY(history.open(QIODevice::ReadOnly)); QCOMPARE(history.readAll(), before);
    }
    void startupRestoresPreferencesWithoutPlayback() {
        Fixture f; QVERIFY(QDir().mkpath(f.data()));
        { SettingsStore settings(f.data()); settings.setValue("lastFile", f.xml); }
        const auto path = QDir(f.data()).filePath("settings.ini");
        { QFile file(path); QVERIFY(file.open(QIODevice::Append)); file.write("\n[Sync]\nautomaticDanmaku=true\ntargetSession=obsolete-player\n"); }
        QFile before(path); QVERIFY(before.open(QIODevice::ReadOnly)); const auto content = before.readAll(); before.close();
        { SettingsStore settings(f.data()); QVERIFY(settings.error().isEmpty()); }
        QVERIFY(before.open(QIODevice::ReadOnly)); QCOMPARE(before.readAll(), content); before.close();
        Harness h(f); QVERIFY(!h.app.running()); QVERIFY(!h.media->active());
        auto* settings = qobject_cast<SettingsStore*>(h.app.settings());
        QVERIFY(!settings->values().contains("targetSession")); QVERIFY(!settings->values().contains("automaticDanmaku"));
        QCOMPARE(qobject_cast<DanmakuSession*>(h.app.danmaku())->filePath(), f.xml);
    }
    void sourcesRequireCurrentVideo() {
        Fixture f; Harness h(f); auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying());
        QSignalSpy search(&h.app, &AppController::requestOnlineDanmakuSearch);
        h.media->loadFile(f.xml); QVERIFY(!h.media->error().isEmpty());
        h.app.loadCachedDanmaku("missing"); h.app.showOnlineDanmakuForAnime("Test");
        QCOMPARE(search.size(), 0); QVERIFY(!h.media->active()); QVERIFY(!h.app.running());
        QVERIFY(!now->canPlay()); QVERIFY(!now->canSeek()); QCOMPARE(now->count(), 0);
        QVERIFY(h.app.metaObject()->indexOfMethod("start(bool)") < 0);
        QVERIFY(h.app.metaObject()->indexOfMethod("selectSession(QString)") < 0);
        QVERIFY(h.app.metaObject()->indexOfProperty("danmaku") < 0);
    }
    void endingTaskRejectsLateSource() {
        Fixture f; f.seed(); Harness h(f); h.open(); QTRY_COMPARE(h.media->total(), 2);
        const auto task = h.media->taskId(); h.media->loadFile(f.second);
        qobject_cast<NowPlayingModel*>(h.app.nowPlaying())->endPlayback();
        QTest::qWait(100); QVERIFY(!h.media->active()); QVERIFY(!h.app.running()); QCOMPARE(h.media->total(), 0);
        QVERIFY(h.media->taskId() != task); QVERIFY(!h.media->canSelectSource());
        h.media->loadCached("missing"); QVERIFY(!h.media->active()); QVERIFY(!h.app.running());
    }
    void unifiedExternalControlNeedsAckAndReadback() {
        Fixture f; f.seed(); Harness h(f); h.open(); QTRY_COMPARE(h.media->total(), 2);
        auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying()); QVERIFY(now->video()); QVERIFY(now->playing());
        disconnect(now, &NowPlayingModel::playbackRequested, h.monitor, &MediaMonitor::requestPlayback);
        QSignalSpy commands(now, &NowPlayingModel::playbackRequested);
        h.media->unload(); QCOMPARE(now->count(), 0); QVERIFY(now->playing()); QVERIFY(now->canPlay());
        QVERIFY(now->canSeek());
        now->togglePlayback(); QCOMPARE(commands.size(), 1); QVERIFY(now->busy());
        const auto id = commands.first()[2].toULongLong();
        h.monitor->playbackControlFinished(id, true, {}); QVERIFY(now->busy()); QVERIFY(now->playing());
        h.sample.playing = false; h.sample.playback = MediaSample::Playback::Paused; h.send();
        QVERIFY(!now->busy()); QVERIFY(!now->playing()); QVERIFY(now->canPlay());
        now->togglePlayback(); QCOMPARE(commands.size(), 2);
        now->endPlayback(); h.monitor->playbackControlFinished(commands.last()[2].toULongLong(), true, {});
        QVERIFY(!now->busy()); QVERIFY(!now->active());
    }
    void failedXmlReplacementKeepsCurrentVideoAndReportsError() {
        Fixture f; f.seed(); Harness h(f); h.open(); QTRY_COMPARE(h.media->total(), 2);
        auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying());
        now->replaceDanmaku(f.directory.filePath("missing.xml")); QTRY_VERIFY(!now->error().isEmpty());
        QVERIFY(!now->error().isEmpty()); QVERIFY(now->video()); QVERIFY(h.media->active()); QCOMPARE(now->count(), 2);
    }
    void videoMetadataSurvivesXmlAndReopen() {
        Fixture f; RecentVideoStore store(f.data()); store.opened(f.video);
        const QVariantMap content{{"animeTitle", "Video title"}, {"episodeTitle", "Episode 1"}, {"imageUrl", "https://example.test/cover.jpg"}};
        store.checkpoint(f.video, 0, 120, {{"kind", "xml"}, {"path", f.xml}}, content);
        Harness h(f); h.open(); QTRY_COMPARE(h.media->total(), 2);
        auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying()); QCOMPARE(now->title(), QString("Video title"));
        h.media->loadFile(f.second); QTRY_COMPARE(h.media->total(), 1); QCOMPARE(now->coverUrl(), content.value("imageUrl").toString());
        h.media->flush(); RecentVideoStore loaded(f.data()); QCOMPARE(loaded.lookup(f.video).value("content").toMap(), content);
        write(f.directory.path(), "Test - 01.mp4", "replacement contents");
        QVERIFY(loaded.lookup(f.video).value("content").toMap().isEmpty());
    }
    void mediaSourceReplacementKeepsConnectionAndHistory() {
        Fixture f; f.seed(); Harness h(f); h.open(); QTRY_COMPARE(h.media->total(), 2);
        const auto generation = h.monitor->generation(); const auto bound = h.service->sessionId();
        h.media->loadFile(f.second); QTRY_COMPARE(h.media->total(), 1);
        QCOMPARE(h.monitor->generation(), generation); QVERIFY(h.service->active()); QVERIFY(h.media->active());
        h.sample.position = 42; h.send(); h.media->flush();
        auto* recent = qobject_cast<RecentVideoStore*>(h.media->recent()); QCOMPARE(recent->lookup(f.video).value("position").toDouble(), 42.0);
        h.media->unload(); QVERIFY(h.media->active()); QVERIFY(!h.app.running()); QCOMPARE(h.service->sessionId(), bound);
        QCOMPARE(h.monitor->generation(), generation); QCOMPARE(recent->lookup(f.video).value("source").toMap().value("kind").toString(), QString("disabled"));
    }
    void resumeRequiresAcceptedRequestAndPositionReadback() {
        Fixture f; f.seed(30); Harness h(f);
        disconnect(h.media, &MediaSession::seekRequested, h.monitor, &MediaMonitor::requestSeek);
        QSignalSpy seeks(h.media, &MediaSession::seekRequested); h.open(MediaSession::Continue);
        QCOMPARE(seeks.size(), 1); QVERIFY(h.media->restoring());
        const auto request = seeks.first()[2].toULongLong();
        h.monitor->seekFinished(request + 1, true, {}); QVERIFY(h.media->restoring());
        h.monitor->seekFinished(request, true, {}); QVERIFY(h.media->restoring());
        h.sample.position = 30; h.send(); QTRY_VERIFY(!h.media->restoring()); QTRY_COMPARE(h.media->total(), 2);
        QVERIFY(!h.media->restoreFailed()); QVERIFY(h.app.running());
    }
    void failedResumePreservesProgressAndCanContinue() {
        Fixture f; f.seed(30); Harness h(f);
        disconnect(h.media, &MediaSession::seekRequested, h.monitor, &MediaMonitor::requestSeek);
        QSignalSpy seeks(h.media, &MediaSession::seekRequested); h.open(MediaSession::Continue);
        h.monitor->seekFinished(seeks.first()[2].toULongLong(), false, "unsupported");
        QVERIFY(h.media->restoreFailed()); h.sample.position = 3; h.send(); h.media->flush();
        QCOMPARE(qobject_cast<RecentVideoStore*>(h.media->recent())->lookup(f.video).value("position").toDouble(), 30.0);
        h.media->continueAtCurrentPosition(); QTRY_COMPARE(h.media->total(), 2); QVERIFY(h.app.running());
    }
    void restoreTimeoutPreservesRecordUntilExplicitRetry() {
        Fixture f; f.seed(30); Harness h(f);
        disconnect(h.media, &MediaSession::seekRequested, h.monitor, &MediaMonitor::requestSeek);
        QSignalSpy seeks(h.media, &MediaSession::seekRequested); h.open(MediaSession::Continue);
        h.monitor->seekFinished(seeks.first()[2].toULongLong(), true, {});
        QTRY_VERIFY_WITH_TIMEOUT(h.media->restoreFailed(), 6500);
        h.media->flush(); QCOMPARE(qobject_cast<RecentVideoStore*>(h.media->recent())->lookup(f.video).value("position").toDouble(), 30.0);
        h.media->retryRestore(); QCOMPARE(seeks.size(), 2); QVERIFY(h.media->restoring());
        h.monitor->seekFinished(seeks.last()[2].toULongLong(), true, {});
        h.sample.position = 30; h.send(); QTRY_COMPARE(h.media->total(), 2); QVERIFY(!h.media->restoreFailed());
    }
    void staleRestoreAndChangedMediaCannotCommit() {
        Fixture f; f.seed(30); Harness h(f);
        disconnect(h.media, &MediaSession::seekRequested, h.monitor, &MediaMonitor::requestSeek);
        QSignalSpy seeks(h.media, &MediaSession::seekRequested); h.open(MediaSession::Continue);
        const auto request = seeks.first()[2].toULongLong();
        qobject_cast<NowPlayingModel*>(h.app.nowPlaying())->endPlayback();
        h.monitor->seekFinished(request, true, {}); QVERIFY(!h.media->active()); QCOMPARE(h.media->total(), 0);
        h.app.stop(); h.open(MediaSession::FromStart); QVERIFY(h.media->restoring());
        h.monitor->seekFinished(seeks.last()[2].toULongLong(), true, {}); h.sample.position = 0; h.send(); QTRY_COMPARE(h.media->total(), 2);
        h.sample.title = "Changed.mp4"; h.sample.sessionToken = 999; h.send();
        QVERIFY(!h.media->active()); QVERIFY(!h.app.running());
    }
};
QTEST_MAIN(WorkflowSessionTests)
#include "WorkflowSessionTests.moc"
