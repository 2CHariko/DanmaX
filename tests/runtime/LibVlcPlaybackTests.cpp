#include "application/AppController.h"
#include "infrastructure/LibVlcBackend.h"
#include "infrastructure/ExternalPlayerDraft.h"
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>

class LibVlcPlaybackTests final : public QObject {
    Q_OBJECT
private slots:
    void presetsPreserveDefaultAndRequireNoExecutable() {
        QTemporaryDir data;
        ExternalPlayerStore store(data.path());
        QCOMPARE(store.selectedId(), QString("libvlc"));
        ExternalPlayerRegistry registry;
        QVERIFY(!registry.find("libvlc")->requiresExecutable());
        QVERIFY(registry.find("libvlc")->available());
        QCOMPARE(store.profiles().size(), 3);
        QVERIFY(store.saveDraft(store.profiles(), "potplayer", store.revision()).value("ok").toBool());
        ExternalPlayerStore loaded(data.path()); QCOMPARE(loaded.selectedId(), QString("potplayer"));
        auto profiles = loaded.profiles(); auto entry = profiles.last().toMap(); entry["executable"] = "C:/unexpected/vlc.exe"; profiles.last() = entry;
        QVERIFY(!loaded.saveDraft(profiles, "libvlc", loaded.revision()).value("ok").toBool());
        QCOMPARE(loaded.selectedId(), QString("potplayer"));
    }
    void missingTargetAndStaleControlsAreRejected() {
        LibVlcBackend backend;
        QVERIFY(!backend.open({}, "missing.mp4").error.isEmpty());
        QVERIFY(!backend.busy());
        QSignalSpy controls(&backend, &MediaPlaybackBackend::playbackControlFinished);
        QSignalSpy seeks(&backend, &MediaPlaybackBackend::seekDispatched);
        backend.requestPlayback({}, false, 1); backend.requestSeek({}, 15, 2);
        QTRY_COMPARE(controls.size(), 1); QVERIFY(!controls.first()[1].toBool());
        QTRY_COMPARE(seeks.size(), 1);
        QCOMPARE(qvariant_cast<SeekDispatch>(seeks.first()[0]).outcome, SeekDispatch::Outcome::Rejected);
        backend.closeOwned("danmax-libvlc:foreign"); QVERIFY(!backend.busy());
    }
    void hiddenViewportDoesNotReclaimDesktopOverlay() {
        QQuickWindow host; host.resize(640, 360);
        QQuickItem item(host.contentItem()); item.setSize(QSizeF(600, 300));
        QWindow overlay;
        VideoViewportController viewport;
        viewport.videoWindow()->setParent(&host);
        viewport.attach(&item); viewport.setOverlay(&overlay); viewport.setActive(true);
        item.setVisible(false);
        const QRect desktop(0, 0, 1920, 1080);
        overlay.setTransientParent(nullptr); overlay.setGeometry(desktop);
        viewport.synchronize();
        QCOMPARE(overlay.geometry(), desktop); QVERIFY(!overlay.transientParent());
        QVERIFY(!viewport.shown());
    }
    void realWorkerAndPublicSeekContract() {
        const auto file = qEnvironmentVariable("DANMAKU_REAL_LIBVLC_VIDEO");
        if (file.isEmpty()) QSKIP("Explicit real video not supplied; run the deployed test with DANMAKU_REAL_LIBVLC_VIDEO.");
        QVERIFY2(LibVlcBackend::preflight().isEmpty(), qPrintable(LibVlcBackend::preflight()));
        QWindow output; output.resize(640, 360); output.create();
        LibVlcBackend backend; backend.setVideoOutput(output.winId());
        MediaMonitor monitor; SeekController seek(&monitor, nullptr, 8000, 1000);
        disconnect(&seek, &SeekController::dispatchRequested, &monitor, &MediaMonitor::requestSeek);
        connect(&seek, &SeekController::dispatchRequested, &backend, &MediaPlaybackBackend::requestSeek);
        connect(&backend, &MediaPlaybackBackend::sampleReceived, &seek, &SeekController::observe);
        connect(&backend, &MediaPlaybackBackend::seekDispatched, &monitor, &MediaMonitor::seekDispatched);
        QSignalSpy samples(&backend, &MediaPlaybackBackend::sampleReceived), controls(&backend, &MediaPlaybackBackend::playbackControlFinished);
        QSignalSpy done(&seek, &SeekController::finished), releases(&backend, &LibVlcBackend::released);
        const auto opened = backend.open({}, file); QVERIFY2(opened.error.isEmpty(), qPrintable(opened.error));
        backend.select(opened.sessionId, 1);
        auto latest = [&] { return samples.isEmpty() ? MediaSample{} : qvariant_cast<MediaSample>(samples.last()[0]); };
        QTRY_VERIFY_WITH_TIMEOUT(latest().usableTimeline() && latest().position > .2, 15000);
        QCOMPARE(latest().evidence, MediaSample::Evidence::Verified);
        QCOMPARE(latest().seekPrecision, MediaSample::SeekPrecision::Unspecified);
        backend.requestPlayback(latest(), false, 3);
        QTRY_COMPARE(controls.size(), 1); QVERIFY(controls.last()[1].toBool());
        QTRY_VERIFY(!latest().playing && latest().playback == MediaSample::Playback::Paused);
        const auto paused = latest().position; QTest::qWait(500); QCOMPARE(latest().position, paused);
        seek.requestSeek(latest(), std::min(20.25, latest().duration / 2), 4);
        QTRY_VERIFY_WITH_TIMEOUT(!done.isEmpty(), 10000); QVERIFY2(done.last()[1].toBool(), qPrintable(done.last()[2].toString()));
        auto stale = latest(); ++stale.sessionToken; backend.requestPlayback(stale, true, 5);
        QTRY_COMPARE(controls.size(), 2); QVERIFY(!controls.last()[1].toBool());
        backend.requestPlayback(latest(), true, 6); QTRY_COMPARE(controls.size(), 3); QTRY_VERIFY(latest().playing);
        const auto old = opened.sessionId;
        backend.closeOwned(old); QTRY_COMPARE_WITH_TIMEOUT(releases.size(), 1, 10000); QVERIFY(!backend.busy());
        const auto reopened = backend.open({}, file); QVERIFY(reopened.error.isEmpty()); QVERIFY(reopened.sessionId != old);
        backend.select(reopened.sessionId, 2); backend.closeOwned(old);
        QTRY_VERIFY_WITH_TIMEOUT(latest().id == reopened.sessionId && latest().usableTimeline(), 15000);
        backend.requestSeek(latest(), std::max(0.0, latest().duration - 1), 8);
        QTRY_VERIFY_WITH_TIMEOUT(latest().playback == MediaSample::Playback::Stopped, 10000);
        QCOMPARE(latest().position, latest().duration);
        backend.cancelOpen(); QTRY_COMPARE_WITH_TIMEOUT(releases.size(), 2, 10000); QVERIFY(!backend.busy());
    }
    void realApplicationRestoreAndTaskHandoff() {
        const auto video = qEnvironmentVariable("DANMAKU_REAL_LIBVLC_VIDEO");
        if (video.isEmpty()) QSKIP("Explicit real video not supplied.");
        QTemporaryDir data;
        const auto xml = data.filePath("source.xml");
        { QFile file(xml); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("<i><d p=\"0,1,25,16777215\">first</d><d p=\"50,1,25,16777215\">later</d></i>"); }
        { RecentVideoStore recent(data.path()); recent.opened(video); recent.checkpoint(video, 25, 120, {{"kind", "xml"}, {"path", xml}}); }
        AppController app(data.path());
        qobject_cast<SettingsStore*>(app.settings())->setDanmakuServers({});
        QQuickWindow overlay; DanmakuItem renderer; renderer.setParentItem(overlay.contentItem()); overlay.resize(640, 360);
        app.attach(&renderer, &overlay);
        auto* media = qobject_cast<MediaSession*>(app.media());
        auto* player = qobject_cast<NowPlayingModel*>(app.nowPlaying());
        auto* monitor = app.findChild<MediaMonitor*>(); QVERIFY(monitor);
        media->openVideo(video, MediaSession::Continue);
        QTRY_VERIFY_WITH_TIMEOUT(media->active() && media->canSelectSource() && !media->restoring() && app.running(), 15000);
        QVERIFY2(!media->restoreFailed(), qPrintable(media->error())); QVERIFY(media->position() >= 24 && media->position() < 35);
        QCOMPARE(player->count(), 2); QVERIFY(app.embeddedVideo());
        player->openStandalone(data.filePath("missing.xml")); QTest::qWait(200);
        QVERIFY(media->active()); QVERIFY(player->video());
        player->openStandalone(xml); QTRY_VERIFY(player->standalone());
        QTRY_VERIFY_WITH_TIMEOUT(!monitor->embeddedBusy(), 10000); QVERIFY(!media->active());
        media->openVideo(video, MediaSession::FromStart);
        QTRY_VERIFY_WITH_TIMEOUT(media->active() && media->canSelectSource() && !media->restoring() && app.running(), 15000);
        QVERIFY(!media->restoreFailed()); QVERIFY(media->position() < 5); QCOMPARE(player->count(), 2);
        app.requestClose(); QTRY_VERIFY_WITH_TIMEOUT(!monitor->embeddedBusy(), 10000); QVERIFY(app.requestClose());
        media->openVideo(video); player->openStandalone(xml); QTest::qWait(100);
        QVERIFY(!player->active());
    }
};
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    qRegisterMetaType<MediaSample>(); qRegisterMetaType<SeekDispatch>();
    LibVlcPlaybackTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "LibVlcPlaybackTests.moc"
