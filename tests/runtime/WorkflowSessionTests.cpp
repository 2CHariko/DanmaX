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
        sample.identity = sample.title;
        QMetaObject::invokeMethod(&app, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
    }
    void open(int intent = MediaSession::Normal) {
        media->openVideo(f.video, intent); sample.title = "Other - 02.mp4"; send(); sample.title = QFileInfo(f.video).fileName(); send();
    }
};
}
class WorkflowSessionTests final : public QObject {
    Q_OBJECT
private slots:
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
        Fixture f;
        QVERIFY(QDir().mkpath(f.data()));
        { SettingsStore settings(f.data()); settings.setValue("automaticDanmaku", true); settings.setValue("targetSession", "danmax-vlc:123:456"); settings.setValue("lastFile", f.xml); }
        Harness h(f); QVERIFY(!h.app.running()); QVERIFY(!h.media->active());
        QVERIFY(!qobject_cast<AutomaticDanmaku*>(h.app.automatic())->enabled());
        QCOMPARE(qobject_cast<SettingsStore*>(h.app.settings())->values().value("targetSession").toString(), QString());
        QCOMPARE(qobject_cast<DanmakuSession*>(h.app.danmaku())->filePath(), f.xml);
    }
    void independentPreparationDoesNotInterruptMedia() {
        Fixture f; f.seed(); Harness h(f); h.open(); QTRY_COMPARE(h.media->total(), 2); QVERIFY(h.app.running());
        auto* settings = qobject_cast<SettingsStore*>(h.app.settings()); settings->setValue("targetSession", "Browser");
        QCOMPARE(settings->values().value("targetSession").toString(), QString("Browser"));
        h.app.loadFile(f.directory.filePath("missing.xml"));
        QTRY_VERIFY(!qobject_cast<DanmakuSession*>(h.app.danmaku())->error().isEmpty());
        QVERIFY(h.media->error().isEmpty()); QVERIFY(h.app.running()); QCOMPARE(h.media->total(), 2);
        QSignalSpy loaded(&h.app, &AppController::loadCompleted); h.app.loadFile(f.second); QTRY_VERIFY(!loaded.isEmpty());
        QVERIFY(qobject_cast<DanmakuSession*>(h.app.danmaku())->error().isEmpty());
        QVERIFY(h.app.running()); QCOMPARE(h.media->total(), 2);
        QCOMPARE(qobject_cast<DanmakuSession*>(h.app.danmaku())->total(), 1);
        QCOMPARE(qobject_cast<PlaybackCoordinator*>(h.app.coordinator())->owner(), PlaybackCoordinator::Media);
        h.app.start(true); QVERIFY(h.app.running()); QVERIFY(h.app.manualMode()); QVERIFY(!h.media->active());
        QCOMPARE(qobject_cast<PlaybackCoordinator*>(h.app.coordinator())->owner(), PlaybackCoordinator::Danmaku);
        QCOMPARE(h.app.total(), 1); QCOMPARE(settings->values().value("targetSession").toString(), QString("Browser"));
    }
    void unifiedIndependentPlaybackAndEnd() {
        Fixture f; Harness h(f); auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying()); QVERIFY(now);
        now->openDanmaku(f.xml); QTRY_VERIFY(h.app.running()); QVERIFY(now->playing()); QVERIFY(now->canSeek());
        QCOMPARE(now->count(), 2); QVERIFY(!now->video());
        now->togglePlayback(); QVERIFY(!now->playing()); now->seek(1); QCOMPARE(now->position(), 1.0);
        now->replaceDanmaku(f.second); QTRY_VERIFY(!now->busy()); QCOMPARE(now->count(), 1); QVERIFY(!now->playing());
        now->togglePlayback(); QVERIFY(now->playing()); h.app.setDanmakuVisible(false); QVERIFY(now->playing());
        now->endPlayback(); QVERIFY(!now->active()); QCOMPARE(qobject_cast<PlaybackCoordinator*>(h.app.coordinator())->owner(), PlaybackCoordinator::None);
        QVERIFY(now->canPlay()); now->togglePlayback(); QVERIFY(now->active());
    }
    void unifiedExternalControlNeedsAckAndReadback() {
        Fixture f; f.seed(); Harness h(f); h.open(); QTRY_COMPARE(h.media->total(), 2);
        auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying()); QVERIFY(now->video()); QVERIFY(now->playing());
        disconnect(now, &NowPlayingModel::playbackRequested, h.monitor, &MediaMonitor::requestPlayback);
        QSignalSpy commands(now, &NowPlayingModel::playbackRequested);
        h.media->unload(); QCOMPARE(now->count(), 0); QVERIFY(now->playing()); QVERIFY(now->canPlay());
        QVERIFY(!now->canSeek());
        now->togglePlayback(); QCOMPARE(commands.size(), 1); QVERIFY(now->busy());
        const auto id = commands.first()[2].toULongLong();
        h.monitor->playbackControlFinished(id, true, {}); QVERIFY(now->busy()); QVERIFY(now->playing());
        h.sample.playing = false; h.sample.playback = MediaSample::Playback::Paused; h.send();
        QVERIFY(!now->busy()); QVERIFY(!now->playing()); QVERIFY(now->canPlay());
        now->togglePlayback(); QCOMPARE(commands.size(), 2);
        now->endPlayback(); h.monitor->playbackControlFinished(commands.last()[2].toULongLong(), true, {});
        QVERIFY(!now->busy()); QVERIFY(!now->active());
    }
    void failedIndependentOpenKeepsCurrentVideoAndReportsError() {
        Fixture f; f.seed(); Harness h(f); h.open(); QTRY_COMPARE(h.media->total(), 2);
        auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying());
        now->openDanmaku(f.directory.filePath("missing.xml")); QTRY_VERIFY(!now->busy());
        QVERIFY(!now->error().isEmpty()); QVERIFY(now->video()); QVERIFY(h.media->active()); QCOMPARE(now->count(), 2);
    }
    void firstFollowPlayResumesOnlyFreshConfirmedContent() {
        Fixture f; Harness h(f); auto* now = qobject_cast<NowPlayingModel*>(h.app.nowPlaying());
        h.app.selectSession("Test.Player"); QSignalSpy loaded(&h.app, &AppController::loadCompleted);
        h.app.loadFile(f.xml); QTRY_VERIFY(!loaded.isEmpty());
        disconnect(now, &NowPlayingModel::playbackRequested, h.monitor, &MediaMonitor::requestPlayback);
        QSignalSpy commands(now, &NowPlayingModel::playbackRequested);
        h.sample.id = "Test.Player"; h.sample.title = "Video.mp4"; h.sample.playing = false; h.sample.playback = MediaSample::Playback::Paused;
        const auto inject = [&] {
            QTest::qWait(2); h.sample.generation = h.monitor->generation(); h.sample.observedAt = MediaMonitor::monotonicSeconds();
            QMetaObject::invokeMethod(&h.app, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, h.sample));
        };
        inject(); QVERIFY(now->canPlay()); now->togglePlayback(); QVERIFY(now->busy()); QVERIFY(h.app.running());
        QCOMPARE(commands.size(), 0); inject(); QCOMPARE(commands.size(), 1); QVERIFY(commands.first()[1].toBool());
        h.monitor->playbackControlFinished(commands.first()[2].toULongLong(), true, {});
        h.sample.playing = true; h.sample.playback = MediaSample::Playback::Playing; inject();
        QVERIFY(!now->busy()); QVERIFY(now->playing());
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
        QSignalSpy loaded(&h.app, &AppController::loadCompleted); h.app.loadFile(f.second); QTRY_VERIFY(!loaded.isEmpty()); h.app.start(true);
        h.monitor->seekFinished(request, true, {}); QVERIFY(!h.media->active()); QCOMPARE(h.app.total(), 1);
        h.app.stop(); h.open(MediaSession::FromStart); QVERIFY(h.media->restoring());
        h.monitor->seekFinished(seeks.last()[2].toULongLong(), true, {}); h.sample.position = 0; h.send(); QTRY_COMPARE(h.media->total(), 2);
        h.sample.title = "Changed.mp4"; h.sample.sessionToken = 999; h.send();
        QVERIFY(!h.media->active()); QVERIFY(!h.app.running());
    }
};
QTEST_MAIN(WorkflowSessionTests)
#include "WorkflowSessionTests.moc"
