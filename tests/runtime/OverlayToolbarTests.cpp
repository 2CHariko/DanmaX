#include "application/AppController.h"
#include "platform/windows/ToolbarInputFilter.h"
#include "platform/windows/ToolbarPointerMonitor.h"
#include "infrastructure/SettingsStore.h"
#include "renderer/DanmakuItem.h"
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QWindow>
#include <QQuickWindow>
#include <QTest>

class OverlayToolbarTests : public QObject {
    Q_OBJECT
private slots:
    void hiddenToolbarPointerSensing() {
        QPoint cursor(-1000, -200);
        ToolbarPointerMonitor monitor(nullptr, [&] { return cursor; });
        QWindow overlay;
        QWindow toolbar;
        overlay.setGeometry(-1920, -200, 1920, 1080);
        toolbar.setGeometry(-1200, -196, 480, 100);
        monitor.attach(&overlay, &toolbar);
        QCOMPARE(monitor.activationRect(), QRect(-1000, -200, 80, 10));
        monitor.setEnabled(true);
        QVERIFY(monitor.hovered());
        QVERIFY(!toolbar.isVisible());
        cursor = QPoint(-921, -191);
        QTRY_VERIFY(monitor.hovered());
        cursor = QPoint(-920, -190);
        QTRY_VERIFY(!monitor.hovered());
        toolbar.show();
        cursor = QPoint(-1150, -130);
        QTRY_VERIFY(monitor.hovered());
        toolbar.hide();
        QTRY_VERIFY(!monitor.hovered());
        overlay.setGeometry(200, 100, 1200, 800);
        QCOMPARE(monitor.activationRect(), QRect(760, 100, 80, 10));
        cursor = QPoint(800, 105);
        QTRY_VERIFY(monitor.hovered());
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
        QVERIFY(!controller.toolbarPresentationAllowed());
        QVERIFY(!controller.toolbarPointer()->property("enabled").toBool());
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
        QTRY_VERIFY(!controller.toolbarPresentationAllowed());
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
        controller.start(false);
        QTRY_VERIFY(controller.overlayVisible());
        playerForeground = false;
        QTRY_VERIFY(!controller.overlayVisible());
        playerForeground = true;
        QTRY_VERIFY(controller.overlayVisible());
        panel.showNormal();
        panelForeground = true;
        playerForeground = false;
        QTRY_VERIFY(controller.overlayVisible());
        QVERIFY(!controller.toolbarPresentationAllowed());
        panelForeground = false;
        // Leaving the panel forces a new player sample even within 250ms.
        QTRY_VERIFY(!controller.overlayVisible());
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
        QVERIFY(layers.reconcile(3, true, true, true));
        QCOMPARE(calls.size(), std::size_t(3));
        QCOMPARE(calls[0].window, overlay.winId());
        QCOMPARE(calls[0].after, notTopmost);
        QCOMPARE(calls[1].after, topmost);
        QCOMPARE(calls.back().window, panel.winId());
        for (const auto& call : calls) QCOMPARE(call.flags, unsigned(0x13)); // NOMOVE|NOSIZE|NOACTIVATE
        calls.clear();
        QVERIFY(layers.reconcile(3, false, true, true));
        QCOMPARE(calls.front().window, panel.winId());
        QCOMPARE(calls.front().after, notTopmost);
        QCOMPARE(calls.back().window, toolbar.winId());
        QCOMPARE(calls.back().after, topmost);
        calls.clear();
        QVERIFY(layers.reconcile(0, true, true));
        QCOMPARE(calls.back().window, panel.winId());
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
        QVERIFY(layers.reconcile(2, true, true));
        calls.clear();
        QVERIFY(layers.reconcile(2, false, false));
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
        QVERIFY(!controller.toolbarPointer()->property("enabled").toBool());
        succeed = false;
        auto* settings = qobject_cast<SettingsStore*>(controller.settings());
        QVERIFY(settings->setValue("onTop", 2));
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
