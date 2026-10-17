#include "application/AppController.h"
#include "platform/windows/ToolbarInputFilter.h"
#include "platform/windows/ToolbarPointerMonitor.h"
#include "infrastructure/SettingsStore.h"
#include "renderer/DanmakuItem.h"
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QWindow>
#include <QQuickWindow>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTest>
#include <algorithm>
#include <cmath>

class OverlayToolbarTests : public QObject {
    Q_OBJECT
private slots:
    void internalRendererLifecycleAndRetainedSource() {
        QTemporaryDir dir;
        AppController controller(dir.path(), nullptr, {}, dir.path());
        auto* monitor = controller.findChild<MediaMonitor*>();
        QObject::disconnect(monitor, nullptr, &controller, nullptr);
        QQuickWindow overlay;
        overlay.resize(800, 600);
        DanmakuItem renderer;
        renderer.setParentItem(overlay.contentItem());
        controller.attach(&renderer, &overlay);
        connect(&controller, &AppController::stateChanged, &overlay,
                [&] { overlay.setVisible(controller.overlayVisible()); });
        QFile source(dir.filePath("source.xml"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        source.write("<i><d p=\"0,5,25,16777215,0,0,x,1\">zero</d><d p=\"10,5,25,16777215,0,0,x,2\">ten</d></i>");
        source.close();
        QSignalSpy loaded(&controller, &AppController::loadCompleted);
        controller.loadFile(source.fileName());
        QTRY_COMPARE(loaded.count(), 1);
        QCOMPARE(controller.playbackState(), AppController::Ready);
        auto* settings = qobject_cast<SettingsStore*>(controller.settings());
        settings->setValue("timeOffset", 30.0);
        controller.setManualMode(true);
        controller.start(true);
        QTRY_VERIFY(controller.position() > 0);
        QCOMPARE(controller.danmakuPosition(), controller.position());
        controller.setManualMode(false);
        QVERIFY(controller.manualMode());
        controller.setRenderTestSession("other-player");
        QVERIFY(controller.playing());
        QSignalSpy states(&controller, &AppController::stateChanged);
        monitor->errorOccurred("independent-isolation");
        QVERIFY(controller.error().isEmpty());
        QCOMPARE(states.count(), 0);
        controller.togglePause();
        const double paused = controller.position();
        QTest::qWait(100);
        QCOMPARE(controller.position(), paused);
        controller.seek(-5);
        QCOMPARE(controller.position(), 0.0);
        controller.seek(6);
        QCOMPARE(controller.position(), 6.0);
        QVERIFY(!controller.playing());
        QVERIFY(renderer.snapshotCount() == 0);
        controller.togglePause();
        QTRY_VERIFY(controller.position() > 6);
        controller.seek(1000);
        QTRY_COMPARE(controller.playbackState(), AppController::Completed);
        controller.seek(3);
        QCOMPARE(controller.playbackState(), AppController::Paused);
        QCOMPARE(controller.position(), 3.0);
        controller.togglePause();
        controller.seek(controller.duration());
        QTRY_VERIFY(controller.completed());
        controller.togglePause();
        QVERIFY(controller.playing());
        QCOMPARE(controller.position(), 0.0);
        QTRY_VERIFY(controller.position() > 0);
        const auto title = controller.sourceTitle();
        const auto path = controller.filePath();
        controller.stop();
        QCOMPARE(controller.playbackState(), AppController::Ready);
        QCOMPARE(controller.total(), 2);
        QCOMPARE(controller.sourceTitle(), title);
        QCOMPARE(controller.filePath(), path);
        QCOMPARE(controller.position(), 0.0);
        QVERIFY(controller.manualMode());
        QVERIFY(!controller.overlayVisible());
        QCOMPARE(renderer.snapshotCount(), 0);
        QCOMPARE(renderer.cacheEntries(), 0);
        controller.stop();
        controller.start(true);
        QCOMPARE(controller.position(), 0.0);
        QTRY_VERIFY(controller.position() > 0);
        controller.unload();
        controller.unload();
        QCOMPARE(controller.playbackState(), AppController::Empty);
        QCOMPARE(controller.total(), 0);
        QVERIFY(controller.sourceTitle().isEmpty());
    }

    void syncStateFreshnessAndOffset() {
        QTemporaryDir dir;
        AppController controller(dir.path(), nullptr, {}, dir.path());
        auto* monitor = controller.findChild<MediaMonitor*>();
        QObject::disconnect(monitor, nullptr, &controller, nullptr);
        QQuickWindow overlay;
        overlay.resize(800, 600);
        DanmakuItem renderer;
        renderer.setParentItem(overlay.contentItem());
        controller.attach(&renderer, &overlay);
        connect(&controller, &AppController::stateChanged, &overlay,
                [&] { overlay.setVisible(controller.overlayVisible()); });
        controller.beginDemo(50);
        controller.stop();
        controller.setRenderTestSession("player-one");
        controller.start(false);
        QCOMPARE(controller.playbackState(), AppController::WaitingForPlayer);
        MediaSample sample;
        sample.id = "player-one";
        sample.identity = "video-one";
        sample.title = "title";
        sample.found = true;
        sample.duration = 100;
        sample.position = 10;
        sample.playback = MediaSample::Playback::Paused;
        auto send = [&](MediaSample value, bool stamp = true) {
            if (stamp) {
                value.generation = monitor->generation();
                value.observedAt = MediaMonitor::monotonicSeconds();
            }
            QVERIFY(QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, value)));
        };
        send(sample);
        QCOMPARE(controller.position(), 10.0);
        QCOMPARE(controller.playbackState(), AppController::Paused);
        auto* settings = qobject_cast<SettingsStore*>(controller.settings());
        settings->setValue("timeOffset", 0.5);
        QCOMPARE(controller.position(), 10.0);
        QCOMPARE(controller.danmakuPosition(), 10.5);
        settings->setValue("timeOffset", -0.5);
        QCOMPARE(controller.danmakuPosition(), 9.5);
        sample.playback = MediaSample::Playback::Playing;
        sample.rate = 2;
        sample.position = 0;
        send(sample);
        QTRY_VERIFY(renderer.snapshotCount() > 0);
        const auto playing = controller.position();
        QTest::qWait(100);
        QVERIFY(controller.position() > playing + 0.1);
        sample.playback = MediaSample::Playback::Stopped;
        send(sample);
        QCOMPARE(controller.playbackState(), AppController::PlayerStopped);
        QCOMPARE(renderer.snapshotCount(), 0);
        QVERIFY(controller.running());
        QCOMPARE(controller.total(), 50);
        sample.playback = MediaSample::Playback::Paused;
        sample.position = 20;
        send(sample);
        QCOMPARE(controller.position(), 20.0);
        sample.generation = monitor->generation();
        sample.observedAt = MediaMonitor::monotonicSeconds() - 3;
        sample.position = 70;
        send(sample, false);
        QCOMPARE(controller.position(), 20.0);
        sample.observedAt = MediaMonitor::monotonicSeconds();
        sample.generation--;
        send(sample, false);
        QCOMPARE(controller.position(), 20.0);
        sample.generation = monitor->generation();
        sample.observedAt = MediaMonitor::monotonicSeconds() - 1;
        send(sample, false);
        QCOMPARE(controller.position(), 20.0);
        sample.found = false;
        send(sample);
        QCOMPARE(controller.playbackState(), AppController::WaitingForPlayer);
        QCOMPARE(controller.position(), 20.0);
        sample.found = true;
        sample.position = 25;
        sample.identity = "video-two";
        send(sample);
        QCOMPARE(controller.position(), 25.0);
        QTRY_COMPARE_WITH_TIMEOUT(controller.playbackState(), AppController::SyncTimeout, 3500);
        QSignalSpy notifications(&controller, &AppController::stateChanged);
        QTest::qWait(120);
        QCOMPARE(notifications.count(), 0);
        sample.position = 30;
        send(sample);
        QCOMPARE(controller.position(), 30.0);
        controller.togglePause();
        controller.seek(1);
        QCOMPARE(controller.position(), 30.0);
        MediaSample old = sample;
        old.generation = monitor->generation();
        old.observedAt = MediaMonitor::monotonicSeconds();
        controller.stop();
        controller.start(false);
        send(old, false);
        QCOMPARE(controller.position(), 0.0);
        sample.position = 40;
        send(sample);
        QCOMPARE(controller.position(), 40.0);
        controller.setRenderTestSession("player-two");
        QCOMPARE(controller.playbackState(), AppController::WaitingForPlayer);
        send(sample);
        QCOMPARE(controller.position(), 0.0);
        controller.stop();
        QVERIFY(!controller.manualMode());
        QCOMPARE(controller.total(), 50);
    }

    void hiddenToolbarPointerSensing() {
        QPoint cursor(-1000, -200);
        ToolbarPointerMonitor monitor(nullptr, [&] { return cursor; });
        QWindow overlay;
        QWindow toolbar;
        overlay.setGeometry(-1920, -200, 1920, 1080);
        toolbar.setGeometry(-1200, -196, 480, 100);
        monitor.attach(&overlay, &toolbar);
        monitor.setActivationWidth(480);
        QCOMPARE(monitor.activationRect(), QRect(-1200, -200, 480, 10));
        monitor.setEnabled(true);
        QVERIFY(monitor.hovered());
        QVERIFY(!toolbar.isVisible());
        cursor = QPoint(-1200, -200);
        QTRY_VERIFY(monitor.hovered());
        cursor = QPoint(-721, -191);
        QTRY_VERIFY(monitor.hovered());
        cursor = QPoint(-720, -191);
        QTRY_VERIFY(!monitor.hovered());
        cursor = QPoint(-1201, -200);
        QTRY_VERIFY(!monitor.hovered());
        cursor = QPoint(-1000, -190);
        QTRY_VERIFY(!monitor.hovered());
        cursor = QPoint(-1160, -195);
        QTRY_VERIFY(monitor.hovered());
        monitor.setActivationWidth(320);
        QVERIFY(!monitor.hovered());
        QCOMPARE(monitor.activationRect(), QRect(-1120, -200, 320, 10));
        toolbar.show();
        cursor = QPoint(-1150, -130);
        QTRY_VERIFY(monitor.hovered());
        toolbar.hide();
        QTRY_VERIFY(!monitor.hovered());
        overlay.setGeometry(200, 100, 1200, 800);
        QCOMPARE(monitor.activationRect(), QRect(640, 100, 320, 10));
        cursor = QPoint(800, 105);
        QTRY_VERIFY(monitor.hovered());
        monitor.setActivationWidth(2000);
        QCOMPARE(monitor.activationRect(), QRect(200, 100, 1200, 10));
        monitor.setActivationWidth(0);
        QVERIFY(monitor.activationRect().isEmpty());
        QVERIFY(!monitor.hovered());
        monitor.setActivationWidth(320);
        monitor.setEnabled(false);
        QVERIFY(!monitor.hovered());
        QVERIFY(!monitor.enabled());
        monitor.attach(nullptr, nullptr);
        monitor.setEnabled(true);
        QVERIFY(!monitor.enabled());
    }

    void panelForegroundAndInitialExposure() {
        QTemporaryDir dir;
        bool panelForeground = true;
        bool playerForeground = true;
        AppController controller(dir.path(), nullptr, [&](const QString&) { return playerForeground; },
                                 dir.path(), [&](QWindow*) { return panelForeground; });
        QQuickWindow overlay;
        QWindow toolbar;
        QWindow panel;
        DanmakuItem renderer;
        renderer.setParentItem(overlay.contentItem());
        panel.show();
        controller.attach(&renderer, &overlay, &toolbar, &panel);
        connect(&controller, &AppController::stateChanged, &overlay,
                [&] { overlay.setVisible(controller.overlayVisible()); });
        controller.beginDemo(50);
        QVERIFY(controller.running());
        QVERIFY(controller.overlayVisible());
        QVERIFY(controller.toolbarPresentationAllowed());
        QVERIFY(controller.toolbarPointer()->property("enabled").toBool());
        QTRY_VERIFY(controller.position() > 0);
        QVERIFY(renderer.snapshotCount() > 0);
        panelForeground = false;
        QTRY_VERIFY(controller.overlayVisible());
        QTRY_VERIFY(controller.toolbarPresentationAllowed());
        QTRY_VERIFY(controller.position() > 0);
        overlay.hide();
        QVERIFY(!controller.toolbarPointer()->property("enabled").toBool());
        overlay.show();
        QVERIFY(controller.toolbarPointer()->property("enabled").toBool());
        panelForeground = true;
        QTRY_VERIFY(controller.toolbarPresentationAllowed());
        QVERIFY(controller.overlayVisible());
        const double before = controller.position();
        QTest::qWait(100);
        QVERIFY(controller.position() > before);
        controller.togglePause();
        const double paused = controller.position();
        QTest::qWait(100);
        QCOMPARE(controller.position(), paused);
        controller.seek(4.0);
        QCOMPARE(controller.position(), 4.0);
        panel.showMinimized();
        QTRY_VERIFY(controller.overlayVisible());
        QCOMPARE(controller.position(), 4.0);
        controller.stop();
        QVERIFY(!controller.overlayVisible());
        QVERIFY(!controller.toolbarPointer()->property("enabled").toBool());

        auto* settings = qobject_cast<SettingsStore*>(controller.settings());
        QVERIFY(settings->setValue("foregroundOnly", true));
        controller.beginDemo(50);
        controller.stop();
        controller.setRenderTestSession("Policy.Player");
        auto* mediaMonitor = controller.findChild<MediaMonitor*>(); QVERIFY(mediaMonitor);
        QObject::disconnect(mediaMonitor, &MediaMonitor::sampleReceived, &controller, nullptr);
        controller.start(false);
        QVERIFY(!controller.overlayVisible());
        MediaSample media;
        media.id = "Policy.Player"; media.found = true; media.duration = 100;
        media.playback = MediaSample::Playback::Paused;
        const auto send = [&] {
            media.generation = mediaMonitor->generation(); media.observedAt = MediaMonitor::monotonicSeconds();
            QVERIFY(QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, media)));
        };
        QTimer heartbeat; connect(&heartbeat, &QTimer::timeout, &controller, send); heartbeat.start(30); send();
        QTRY_VERIFY(controller.overlayVisible());
        playerForeground = false;
        QTRY_VERIFY(!controller.overlayVisible());
        playerForeground = true;
        QTRY_VERIFY(controller.overlayVisible());
        panel.showNormal();
        panelForeground = true;
        playerForeground = false;
        QTRY_VERIFY(controller.overlayVisible());
        QVERIFY(controller.toolbarPresentationAllowed());
        panelForeground = false;
        // Leaving the panel forces a new player sample even within 250ms.
        QTRY_VERIFY(!controller.overlayVisible());
        panelForeground = true; media.found = false; send();
        QVERIFY(!controller.overlayVisible()); QVERIFY(!controller.toolbarPresentationAllowed());
        controller.stop();
    }

    void layerOrderingAndLifecycle() {
        QWindow panel, overlay, toolbar;
        panel.show(); overlay.show(); toolbar.show();
        struct Call { quintptr window, after; unsigned flags; };
        std::vector<Call> calls;
        WindowLayerController layers(nullptr, [&](quintptr window, quintptr after, unsigned flags) {
            calls.push_back({window, after, flags}); return true;
        });
        const auto topmost = ~quintptr(0);
        const auto notTopmost = ~quintptr(1);
        layers.attach(&panel, &overlay, &toolbar);
        QVERIFY(layers.reconcile(1, true, true, true));
        QCOMPARE(calls.size(), std::size_t(3));
        QCOMPARE(calls[0].window, overlay.winId());
        QCOMPARE(calls[0].after, topmost);
        QCOMPARE(calls[1].window, panel.winId());
        QCOMPARE(calls.back().window, toolbar.winId());
        for (const auto& call : calls) QCOMPARE(call.flags, unsigned(0x213)); // NOMOVE|NOSIZE|NOACTIVATE|NOOWNERZORDER
        calls.clear();
        QVERIFY(layers.reconcile(1, false, true, true));
        QCOMPARE(calls.front().window, panel.winId());
        QCOMPARE(calls.front().after, notTopmost);
        QCOMPARE(calls.back().window, toolbar.winId());
        QCOMPARE(calls.back().after, topmost);
        calls.clear();
        QVERIFY(layers.reconcile(0, true, true));
        QCOMPARE(calls[calls.size() - 2].window, panel.winId());
        QCOMPARE(calls.back().after, quintptr(0)); // HWND_TOP, ordinary band
        QVERIFY(layers.reconcile(1, true, true));
        calls.clear();
        const auto oldHandle = panel.winId();
        panel.destroy();
        QVERIFY(!calls.empty());
        QCOMPARE(calls.back().window, oldHandle);
        QCOMPARE(calls.back().after, notTopmost);
        panel.create();
        panel.show();
        QVERIFY(layers.reconcile(1, true, true));
        calls.clear();
        layers.attach(nullptr, nullptr, nullptr);
        QVERIFY(!calls.empty());
        QCOMPARE(calls.back().after, notTopmost);

        // A pre-existing topmost main window must remain topmost on exit.
        panel.setFlag(Qt::WindowStaysOnTopHint, true);
        panel.show();
        layers.attach(&panel, &overlay, &toolbar);
        QVERIFY(layers.reconcile(1, true, true));
        calls.clear();
        QVERIFY(layers.reconcile(1, false, false));
        QVERIFY(calls.empty());
    }

    void panelLayerFailureAndRecovery() {
        QTemporaryDir dir;
        QQuickWindow overlay;
        QWindow panel;
        panel.show();
        DanmakuItem renderer;
        renderer.setParentItem(overlay.contentItem());
        bool succeed = false;
        AppController controller(dir.path(), nullptr, {}, dir.path(), [](QWindow*) { return true; }, {},
            [&](quintptr, quintptr, unsigned) { return succeed; });
        controller.attach(&renderer, &overlay, nullptr, &panel);
        connect(&controller, &AppController::stateChanged, &overlay,
                [&] { overlay.setVisible(controller.overlayVisible()); });
        controller.beginDemo(50);
        QVERIFY(!controller.overlayVisible());
        QVERIFY(!controller.toolbarPresentationAllowed());
        QTest::qWait(100);
        QCOMPARE(controller.position(), 0.0);
        succeed = true;
        QTRY_VERIFY(controller.overlayVisible());
        QTRY_VERIFY(controller.position() > 0);
        QVERIFY(renderer.snapshotCount() > 0);
        QVERIFY(controller.toolbarPointer()->property("enabled").toBool());
        succeed = false;
        controller.findChild<WindowLayerController*>()->invalidate();
        QTRY_VERIFY(!controller.overlayVisible());
        const auto hiddenPosition = controller.position();
        QTest::qWait(100);
        QVERIFY(controller.position() > hiddenPosition);
        controller.togglePause();
        const auto frozen = controller.position();
        controller.seek(3);
        succeed = true;
        QTRY_VERIFY(controller.overlayVisible());
        QCOMPARE(controller.position(), 3.0);
        QVERIFY(frozen > 0);
        controller.stop();
    }

    void toolbarFilterAttachment() {
        ToolbarInputFilter filter;
        QCOMPARE(filter.window(), nullptr);

        QWindow window;
        filter.attach(&window);
        QCOMPARE(filter.window(), &window);

        filter.attach(nullptr);
        QCOMPARE(filter.window(), nullptr);
    }

    void dualWindowAttachment() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        AppController controller(dir.path(), nullptr, {}, dir.path());
        QWindow overlayWindow;
        QWindow toolbarWindow;
        DanmakuItem renderer;

        // 验证三参数双窗口 attach 挂接
        controller.attach(&renderer, &overlayWindow, &toolbarWindow);
        QCOMPARE(controller.toolbarInputFilter()->window(), &toolbarWindow);
    }

    void controllerStepOperations() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        AppController controller(dir.path(), nullptr, {}, dir.path());
        auto* settings = qobject_cast<SettingsStore*>(controller.settings());
        QVERIFY(settings);

        // 1. 时间轴步进 stepTimeOffset
        settings->setValue(QStringLiteral("timeOffset"), 0.0);
        controller.stepTimeOffset(0.5);
        QCOMPARE(settings->values().value(QStringLiteral("timeOffset")).toDouble(), 0.5);
        controller.stepTimeOffset(-0.5);
        QCOMPARE(settings->values().value(QStringLiteral("timeOffset")).toDouble(), 0.0);

        // 边界保护 [-120, 120]
        settings->setValue(QStringLiteral("timeOffset"), 119.8);
        controller.stepTimeOffset(0.5);
        QCOMPARE(settings->values().value(QStringLiteral("timeOffset")).toDouble(), 120.0);

        // 2. 字号调节 stepFontSize
        settings->setValue(QStringLiteral("fontSize"), 24);
        controller.stepFontSize(2);
        QCOMPARE(settings->values().value(QStringLiteral("fontSize")).toInt(), 26);
        controller.stepFontSize(-2);
        QCOMPARE(settings->values().value(QStringLiteral("fontSize")).toInt(), 24);

        // 边界保护 [10, 72]
        settings->setValue(QStringLiteral("fontSize"), 71);
        controller.stepFontSize(2);
        QCOMPARE(settings->values().value(QStringLiteral("fontSize")).toInt(), 72);

        // 3. 弹幕通道显隐 danmakuVisible
        QSignalSpy visibleSpy(&controller, &AppController::danmakuVisibleChanged);
        QVERIFY(controller.danmakuVisible());
        controller.setDanmakuVisible(false);
        QVERIFY(!controller.danmakuVisible());
        QCOMPARE(visibleSpy.count(), 1);
        controller.setDanmakuVisible(true);
        QVERIFY(controller.danmakuVisible());
        QCOMPARE(visibleSpy.count(), 2);

        // 4. 激活主窗口信号 requestShowMainWindow
        QSignalSpy showMainSpy(&controller, &AppController::requestShowMainWindow);
        controller.showMainWindow();
        QCOMPARE(showMainSpy.count(), 1);
    }

    void clearActiveDanmaku() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        AppController controller(dir.path(), nullptr, {}, dir.path());
        // 验证清屏调用在无渲染器与有状态下的安全排空与指标重置
        controller.clearActiveDanmaku();
        QCOMPARE(controller.metrics().value(QStringLiteral("active")).toInt(), 0);
    }
};

QTEST_MAIN(OverlayToolbarTests)
#include "OverlayToolbarTests.moc"
