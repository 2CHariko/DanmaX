#pragma once
#include "application/AppController.h"
#include "platform/windows/PlayerProcessIdentity.h"
#include "platform/windows/PotPlayerLauncher.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSignalSpy>
#include <QScopeGuard>
#include <QTest>
#include <windows.h>

inline void closeProbeProcess(const QString& key) {
    if (!PlayerProcessIdentity::alive(key)) return;
    const auto handle = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, key.section(':', 1, 1).toUInt());
    if (!handle) return;
    TerminateProcess(handle, 0); WaitForSingleObject(handle, 3000); CloseHandle(handle);
}
inline void verifyMediaWorkflow(AppController& app, MediaMonitor* monitor, VideoOpenService* service,
                                MediaSample& sample, const QString& video, const QString& directory, const QString& ownedProcess,
                                bool allowRestoreFailure = false) {
    auto* media = qobject_cast<MediaSession*>(app.media()); QVERIFY(media);
    auto* recent = qobject_cast<RecentVideoStore*>(media->recent()); QVERIFY(recent);
    QTRY_VERIFY_WITH_TIMEOUT(app.running() && !media->restoring() && !media->restoreFailed(), 10000);
    QSignalSpy seeks(monitor, &MediaMonitor::seekFinished), controls(monitor, &MediaMonitor::playbackControlFinished);
    quint64 request = quint64(1) << 60;
    const auto seek = [&](double position) {
        monitor->requestSeek(sample, position, ++request);
    };
    QVERIFY2(sample.canSeek, "The real player does not advertise playback-position control.");
    seek(60.25); QTRY_VERIFY_WITH_TIMEOUT(!seeks.isEmpty(), 5000); QVERIFY2(seeks.last()[1].toBool(), qPrintable(seeks.last()[2].toString()));
    QElapsedTimer seekWait; seekWait.start();
    while (std::abs(media->position() - (60.25 + (sample.playing ? seekWait.elapsed() / 1000.0 * sample.rate : 0))) > 1
           && seekWait.elapsed() < 5000) QTest::qWait(50);
    const bool seekVerified = seekWait.elapsed() < 5000;
    QVERIFY2(seekVerified || allowRestoreFailure, "Real seek did not reach the expected position.");
    media->flush(); const auto savedPosition = recent->lookup(video).value("position").toDouble();
    const auto independentBinding = qobject_cast<SettingsStore*>(app.settings())->values().value("targetSession");
    QSignalSpy confirmed(service, &VideoOpenService::confirmed);
    if (!service->sessionId().startsWith("danmax-vlc:"))
        QTRY_VERIFY_WITH_TIMEOUT(PotPlayerLauncher::preflight(service->playerPath()).isEmpty(), 5000);
    media->openVideo(video, MediaSession::Continue);
    QTRY_COMPARE_WITH_TIMEOUT(confirmed.size(), 1, 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!media->restoring() && (media->restoreFailed() || app.running()), 10000);
    const bool continueVerified = !media->restoreFailed();
    if (!continueVerified) {
        QVERIFY(allowRestoreFailure); media->flush();
        QCOMPARE(recent->lookup(video).value("position").toDouble(), savedPosition);
        media->continueAtCurrentPosition(); QTRY_VERIFY_WITH_TIMEOUT(app.running(), 10000);
    }
    QCOMPARE(qobject_cast<SettingsStore*>(app.settings())->values().value("targetSession"), independentBinding);
    confirmed.clear();
    if (!service->sessionId().startsWith("danmax-vlc:"))
        QTRY_VERIFY_WITH_TIMEOUT(PotPlayerLauncher::preflight(service->playerPath()).isEmpty(), 5000);
    media->openVideo(video, MediaSession::FromStart);
    QTRY_VERIFY2_WITH_TIMEOUT(confirmed.size() == 1, qPrintable(service->status() + "; " + media->error()), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!media->restoring() && (media->restoreFailed() || app.running()), 10000);
    const bool fromStartVerified = !media->restoreFailed() && media->position() < 5;
    if (!fromStartVerified) {
        QVERIFY(allowRestoreFailure); QVERIFY(media->restoreFailed());
        media->continueAtCurrentPosition(); QTRY_VERIFY_WITH_TIMEOUT(app.running(), 10000);
    }
    if (!sample.playing) {
        monitor->requestPlayback(sample, true, ++request);
        QTRY_VERIFY_WITH_TIMEOUT(sample.playing && app.playing(), 5000); controls.clear();
    }
    const auto xml = QDir(directory).filePath("selected.xml");
    QFile file(xml); QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<i><d p=\"0,1,25,16777215\">Media workflow</d><d p=\"61,1,25,16777215\">Sync</d></i>"); file.close();
    auto* now = qobject_cast<NowPlayingModel*>(app.nowPlaying()); QVERIFY(now); QVERIFY(now->video());
    const auto contentTitle = now->title(), contentCover = now->coverUrl();
    const auto generation = monitor->generation(); media->loadFile(xml);
    QTRY_VERIFY2_WITH_TIMEOUT(media->total() == 2, qPrintable(QString("active=%1 connected=%2; %3; %4")
        .arg(media->active()).arg(media->connected()).arg(media->status(), media->error())), 5000);
    QTRY_COMPARE(media->sourceTitle(), xml);
    QCOMPARE(monitor->generation(), generation); QVERIFY(service->active()); QVERIFY(media->connected());
    QCOMPARE(now->title(), contentTitle); QCOMPARE(now->coverUrl(), contentCover);
    QTRY_VERIFY(now->canPlay()); now->togglePlayback();
    QTRY_VERIFY_WITH_TIMEOUT(!now->busy() && !now->playing(), 5000);
    now->togglePlayback(); QTRY_VERIFY_WITH_TIMEOUT(!now->busy() && now->playing(), 5000);
    media->unload(); QCOMPARE(now->count(), 0); QVERIFY(now->playing()); QVERIFY(now->canPlay());
    media->loadFile(xml); QTRY_COMPARE(media->total(), 2);
    media->loadFile(QDir(directory).filePath("missing.xml"));
    auto* automatic = qobject_cast<AutomaticDanmaku*>(media->automatic()); QVERIFY(automatic);
    QTRY_VERIFY_WITH_TIMEOUT(automatic->playbackRecovery()->active() && !app.playing(), 5000);
    media->loadFile(xml); QTRY_VERIFY_WITH_TIMEOUT(!media->loading() && !automatic->playbackRecovery()->active() && app.playing(), 5000);
    monitor->requestPlayback(sample, false, ++request); QTRY_VERIFY_WITH_TIMEOUT(!controls.isEmpty() && !app.playing(), 5000);
    QVERIFY(controls.last()[1].toBool());
    media->loadFile(QDir(directory).filePath("missing-paused.xml")); QTRY_VERIFY(automatic->playbackRecovery()->active());
    media->loadFile(xml); QTRY_VERIFY_WITH_TIMEOUT(!media->loading() && !automatic->playbackRecovery()->active(), 5000);
    QTest::qWait(250); QVERIFY(!app.playing());
    controls.clear(); monitor->requestPlayback(sample, true, ++request);
    QTRY_VERIFY_WITH_TIMEOUT(!controls.isEmpty() && app.playing(), 5000); QVERIFY(controls.last()[1].toBool());
    seeks.clear(); seek(80); QTRY_VERIFY_WITH_TIMEOUT(!seeks.isEmpty(), 5000); QVERIFY(seeks.last()[1].toBool());
    QTRY_VERIFY_WITH_TIMEOUT(media->position() >= 79 && media->position() < 85, 5000); media->flush();
    bool closeCycle = false, restoredAfterClose = false;
    if (PlayerProcessIdentity::alive(ownedProcess)) {
        closeProbeProcess(ownedProcess); QTRY_VERIFY_WITH_TIMEOUT(!app.targetSessionConnected(), 5000);
        QVERIFY(recent->lookup(video).value("position").toDouble() >= 79);
        confirmed.clear(); media->openVideo(video, MediaSession::Continue);
        QTRY_COMPARE_WITH_TIMEOUT(confirmed.size(), 1, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(!media->restoring() && (media->restoreFailed() || app.running()), 10000);
        restoredAfterClose = !media->restoreFailed() && media->position() >= 79;
        if (!restoredAfterClose) {
            QVERIFY(allowRestoreFailure); QVERIFY(media->restoreFailed());
            media->continueAtCurrentPosition(); QTRY_VERIFY_WITH_TIMEOUT(app.running(), 10000);
        }
        closeCycle = true;
    }
    QSaveFile output(QDir(directory).filePath("workflow.json")); QVERIFY(output.open(QIODevice::WriteOnly));
    output.write(QJsonDocument(QJsonObject{{"seek", seekVerified}, {"continueWatching", continueVerified}, {"fromStart", fromStartVerified},
        {"xmlKeepsConnection", true}, {"matchingFailurePauseResume", true}, {"originalPausePreserved", true},
        {"ownedCloseAndReopen", closeCycle}, {"restoredAfterClose", restoredAfterClose}, {"nowPlayingPlayPause", true},
        {"position", media->position()}, {"error", media->error()}}).toJson());
    QVERIFY(output.commit());
}
