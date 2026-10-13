
#include <QGuiApplication>
#include <QCursor>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QQuickItemGrabResult>
#include <QTest>
#include <QDir>
#include <QSignalSpy>
#include <QFontDatabase>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWheelEvent>
#include <QStyleHints>
#include <memory>
#if defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
#include "application/AppController.h"
#include "renderer/DanmakuItem.h"
#include "infrastructure/SettingsStore.h"
#include "infrastructure/LogModel.h"
#include "platform/windows/WindowBackdropController.h"
#include <QWKQuick/qwkquickglobal.h>

class UiWorkflowTests : public QObject {
    Q_OBJECT
private slots:
    void debugHudLongTitleLayout() {
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        QQmlComponent fixture(&engine);
        fixture.setData(R"(
            import QtQml
            QtObject {
                property QtObject settings: QtObject {
                    property var values: ({ debug: true, debugPosition: "top_left" })
                }
                property bool danmakuVisible: true
                property string status: "播放器已暂停"
                property string mediaTitle: ""
                property real position: 12345.6
                property var metrics: ({ active: 5000, dropped: 12345, presentedPerSecond: 165,
                    updatesPerSecond: 165, p95Ms: 123.4, memoryMiB: 2048.5,
                    textureEstimatedBytes: 536870912, textureBudgetBytes: 1073741824,
                    textFallbackNodes: 5000, textureFallbackReasons: { capacity: 12345 } })
            }
        )", QUrl());
        QVERIFY2(fixture.isReady(), qPrintable(fixture.errorString()));
        std::unique_ptr<QObject> backend(fixture.create());
        QVERIFY(backend);
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/OverlayWindow.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties({{"backend", QVariant::fromValue(backend.get())}}));
        auto* window = qobject_cast<QQuickWindow*>(object.get());
        QVERIFY(window);
        auto* panel = window->findChild<QQuickItem*>("overlayDebugPanel");
        auto* title = window->findChild<QQuickItem*>("overlayDebugTitle");
        auto* status = window->findChild<QQuickItem*>("overlayDebugStatus");
        auto* metrics = window->findChild<QQuickItem*>("overlayDebugMetrics");
        QVERIFY(panel && title && status && metrics);
        window->resize(1024, 720);
        window->show();
        QTRY_VERIFY(window->isExposed());
        QTRY_COMPARE(panel->width(), 320.0);
        QCOMPARE(status->property("lineCount").toInt(), 1);
        QCOMPARE(metrics->property("lineCount").toInt(), 7);
        const auto completeMetrics = metrics->property("text").toString();
        QVERIFY(completeMetrics.contains("12345.6 s"));
        QVERIFY(completeMetrics.contains("512.0"));
        QVERIFY(completeMetrics.contains("1024"));
        QVERIFY(!metrics->property("truncated").toBool());
        const auto emptyHeight = panel->height();
        QVERIFY(!title->isVisible());
        for (const auto& value : {QStringLiteral("短标题"), QStringLiteral("长媒体标题，继续显示而不撑开调试面板。").repeated(50),
                QStringLiteral("https://example.test/") + QString(1000, 'W'),
                QString::fromUtf8("播放视频😀🎵测试").repeated(100)}) {
            QVERIFY(backend->setProperty("mediaTitle", value));
            QTRY_VERIFY(title->isVisible());
            QTRY_COMPARE(title->property("text").toString(), value);
            QCOMPARE(panel->width(), 320.0);
            QVERIFY(title->property("lineCount").toInt() <= 2);
            QCOMPARE(title->property("truncated").toBool(), value.size() > 100);
            QCOMPARE(metrics->property("text").toString(), completeMetrics);
            QCOMPARE(metrics->property("lineCount").toInt(), 7);
            QCOMPARE(backend->property("mediaTitle").toString(), value);
        }
        const auto literalTitle = QStringLiteral("<b>媒体标题</b>\r\n第二行\t片段\u2028尾部");
        QVERIFY(backend->setProperty("mediaTitle", literalTitle));
        QTRY_COMPARE(title->property("text").toString(), QStringLiteral("<b>媒体标题</b> 第二行 片段 尾部"));
        QCOMPARE(title->property("textFormat").toInt(), 0); // Text.PlainText
        QCOMPARE(backend->property("mediaTitle").toString(), literalTitle);
        QVERIFY(backend->setProperty("mediaTitle", "\r\n\t "));
        QTRY_VERIFY(!title->isVisible());
        QTRY_COMPARE(panel->height(), emptyHeight);

        auto* settings = backend->property("settings").value<QObject*>();
        QVERIFY(settings);
        QVERIFY(backend->setProperty("mediaTitle", QStringLiteral("超长标题").repeated(200)));
        for (const auto width : {1024, 240, 160}) {
            window->resize(width, 720);
            QTRY_COMPARE(panel->width(), double(std::min(320, width - 32)));
            QCOMPARE(metrics->property("text").toString(), completeMetrics);
            QVERIFY(!metrics->property("truncated").toBool());
            QVERIFY(title->property("lineCount").toInt() <= 2);
            for (const auto position : {"top_left", "top_right", "bottom_left", "bottom_right"}) {
                QVERIFY(settings->setProperty("values", QVariantMap{{"debug", true}, {"debugPosition", position}}));
                const QString corner = QString::fromLatin1(position);
                QTRY_COMPARE(panel->x(), corner.endsWith("right") ? window->width() - panel->width() - 16 : 16.0);
                QTRY_COMPARE(panel->y(), corner.startsWith("bottom") ? window->height() - panel->height() - 16 : 16.0);
                QVERIFY(panel->x() >= 0 && panel->y() >= 0);
                QVERIFY(panel->x() + panel->width() <= window->width());
                QVERIFY(panel->y() + panel->height() <= window->height());
            }
        }
        window->resize(1024, 720);
        QVERIFY(panel->setProperty("textPixelSize", 24.0));
        QTRY_VERIFY(qAbs(panel->width() - 320.0 * 24 / 14) < 0.01);
        QVERIFY(title->property("lineCount").toInt() <= 2);
        QCOMPARE(metrics->property("lineCount").toInt(), 7);
        QVERIFY(!metrics->property("truncated").toBool());
        QVERIFY(panel->setProperty("textPixelSize", 14.0));
        QTRY_COMPARE(panel->width(), 320.0);
#if defined(QT_DEBUG)
        const auto buildName = QStringLiteral("debug");
#else
        const auto buildName = QStringLiteral("release");
#endif
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/debug-hud/") + buildName +
                              QStringLiteral("-dpr%1").arg(window->devicePixelRatio());
        QDir().mkpath(evidence);
        QVERIFY(window->grabWindow().save(evidence + "/long-title.png"));
        const auto hudGrab = panel->grabToImage();
        QTRY_VERIFY(hudGrab && !hudGrab->image().isNull());
        QVERIFY(hudGrab->saveToFile(evidence + "/panel.png"));
        window->resize(240, 720);
        QTRY_COMPARE(panel->width(), 208.0);
        QVERIFY(window->grabWindow().save(evidence + "/narrow.png"));
        QVERIFY(window->flags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(window->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QCOMPARE(warnings.count(), 0);
    }

    void nativeLayerOrderingWithoutActivation() {
#if defined(Q_OS_WIN)
        QQuickWindow panel, overlay, toolbar;
        panel.setFlags(Qt::Window | Qt::WindowDoesNotAcceptFocus);
        panel.setGeometry(180, 180, 600, 400);
        panel.setColor(QColor("#303030"));
        overlay.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus | Qt::WindowTransparentForInput);
        overlay.setGeometry(panel.screen()->geometry());
        overlay.setColor(Qt::transparent);
        toolbar.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        toolbar.setGeometry(400, 0, 300, 48);
        toolbar.setColor(QColor("#404040"));
        overlay.setTransientParent(nullptr);
        toolbar.setTransientParent(&overlay);
        panel.show(); overlay.show();
        QTest::qWait(100);
        const auto foreground = GetForegroundWindow();
        const auto above = [](HWND upper, HWND lower) {
            for (auto current = GetWindow(lower, GW_HWNDPREV); current; current = GetWindow(current, GW_HWNDPREV))
                if (current == upper) return true;
            return false;
        };
        WindowLayerController layers;
        layers.attach(&panel, &overlay, &toolbar);
        const auto panelHandle = reinterpret_cast<HWND>(panel.winId());
        const auto overlayHandle = reinterpret_cast<HWND>(overlay.winId());
        for (int strategy = 0; strategy < 4; ++strategy) {
            overlay.setFlag(Qt::WindowStaysOnTopHint, strategy > 0);
            toolbar.setFlag(Qt::WindowStaysOnTopHint, strategy > 0);
            QVERIFY(layers.reconcile(strategy, true, true, true));
            QVERIFY(above(panelHandle, overlayHandle));
            QCOMPARE(bool(GetWindowLongPtrW(panelHandle, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QCOMPARE(bool(GetWindowLongPtrW(overlayHandle, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QVERIFY(!panel.flags().testFlag(Qt::WindowStaysOnTopHint));
            QCOMPARE(GetForegroundWindow(), foreground);
            QVERIFY(layers.reconcile(strategy, false, true, true));
            QVERIFY(!(GetWindowLongPtrW(panelHandle, GWL_EXSTYLE) & WS_EX_TOPMOST));
        }
        WNDCLASSW probeClass{};
        probeClass.lpfnWndProc = DefWindowProcW;
        probeClass.hInstance = GetModuleHandleW(nullptr);
        probeClass.lpszClassName = L"DanmaXLayerNoActivateProbe";
        QVERIFY(RegisterClassW(&probeClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
        struct NativeOwner {
            HWND window;
            ~NativeOwner() { if (IsWindow(window)) DestroyWindow(window); }
        } dialog{CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, probeClass.lpszClassName, L"Layer owner probe",
            WS_POPUP | WS_CAPTION, 240, 240, 300, 140, panelHandle, nullptr, probeClass.hInstance, nullptr)};
        QVERIFY(dialog.window);
        ShowWindow(dialog.window, SW_SHOWNOACTIVATE);
        NativeOwner nested{CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, probeClass.lpszClassName, L"Layer nested probe",
            WS_POPUP | WS_CAPTION, 280, 280, 240, 100, dialog.window, nullptr, probeClass.hInstance, nullptr)};
        QVERIFY(nested.window);
        ShowWindow(nested.window, SW_SHOWNOACTIVATE);
        QVERIFY(layers.reconcile(3, true, true, true));
        QVERIFY(above(nested.window, dialog.window));
        QVERIFY(above(dialog.window, panelHandle));
        QVERIFY(above(panelHandle, overlayHandle));
        QCOMPARE(GetForegroundWindow(), foreground);
        QVERIFY(layers.reconcile(3, false, false));
        QVERIFY(!(GetWindowLongPtrW(panelHandle, GWL_EXSTYLE) & WS_EX_TOPMOST));
        QVERIFY(!(GetWindowLongPtrW(dialog.window, GWL_EXSTYLE) & WS_EX_TOPMOST));
        QVERIFY(!(GetWindowLongPtrW(nested.window, GWL_EXSTYLE) & WS_EX_TOPMOST));
        DestroyWindow(nested.window);
        DestroyWindow(dialog.window);
        QVERIFY(layers.reconcile(3, true, true));
        panel.destroy();
        panel.create(); panel.show();
        QVERIFY(layers.reconcile(3, true, true));
        const auto recreated = reinterpret_cast<HWND>(panel.winId());
        QVERIFY(GetWindowLongPtrW(recreated, GWL_EXSTYLE) & WS_EX_TOPMOST);
        QVERIFY(above(recreated, overlayHandle));
        layers.attach(nullptr, nullptr, nullptr);
        QVERIFY(!(GetWindowLongPtrW(recreated, GWL_EXSTYLE) & WS_EX_TOPMOST));
        QCOMPARE(GetForegroundWindow(), foreground);
#if defined(QT_DEBUG)
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/panel-layer/debug");
#else
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/panel-layer/release");
#endif
        QDir().mkpath(evidence);
        QFile report(evidence + "/native-order.json");
        QVERIFY(report.open(QIODevice::WriteOnly));
        const QJsonObject result{{"strategies", 4}, {"nestedOwners", true}, {"surfaceRecreated", true},
            {"baselineRestored", true}, {"foregroundPreserved", true}, {"foregroundPolicySimulated", true}};
        QVERIFY(report.write(QJsonDocument(result).toJson()) > 0);
#endif
    }

    void nativeToolbarMousePolicy() {
#if defined(Q_OS_WIN)
        QTemporaryDir directory;
        AppController backend(directory.path(), nullptr, {}, directory.path());
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* panel = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(panel);
        auto* overlay = panel->findChild<QQuickWindow*>("danmakuOverlay");
        auto* toolbar = panel->findChild<QQuickWindow*>("danmakuToolbarWindow");
        QVERIFY(overlay && toolbar);
        panel->showMinimized();
        QQuickWindow player;
        player.setFlags(Qt::Window | Qt::FramelessWindowHint);
        player.setGeometry(overlay->geometry());
        player.setColor(QColor("#303030"));
        player.show();
        player.requestActivate();
        QTest::qWait(200);
        const auto playerHandle = reinterpret_cast<HWND>(player.winId());
        if (GetForegroundWindow() != playerHandle)
            QSKIP("Windows foreground was not granted to the input fixture; no global mouse input was injected.");
        struct CursorRestore { QPoint point; ~CursorRestore() { QCursor::setPos(point); } } restore{QCursor::pos()};
        struct PressCounter final : QObject {
            int presses{};
            bool eventFilter(QObject*, QEvent* event) override {
                if (event->type() == QEvent::MouseButtonPress) ++presses;
                return false;
            }
        } counter;
        player.installEventFilter(&counter);
        QCursor::setPos(overlay->x() + 10, overlay->y() + 100);
        backend.beginDemo(50);
        QTRY_VERIFY(overlay->isExposed());
        QVERIFY(!toolbar->isVisible());
        POINT point{};
        GetCursorPos(&point);
        if (GetForegroundWindow() != playerHandle || WindowFromPoint(point) != playerHandle)
            QSKIP("The desktop input fixture is occluded; no global mouse input was injected.");
        INPUT inputs[2]{};
        inputs[0].type = inputs[1].type = INPUT_MOUSE;
        inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
        QCOMPARE(SendInput(2, inputs, sizeof(INPUT)), UINT(2));
        QTRY_COMPARE(counter.presses, 1);
        QCursor::setPos(overlay->x() + overlay->width() / 2, overlay->y() + 2);
        QTRY_COMPARE(toolbar->property("phase").toString(), QString("shown"));
        const auto toolbarHandle = reinterpret_cast<HWND>(toolbar->winId());
        QVERIFY(GetForegroundWindow() != toolbarHandle);
        auto* button = toolbar->findChild<QQuickItem*>("toolbarOpacityButton");
        auto* popup = toolbar->findChild<QQuickItem*>("toolbarOpacityPopup");
        QVERIFY(button && popup);
        QCursor::setPos(toolbar->mapToGlobal(button->mapToScene(
            QPointF(button->width() / 2, button->height() / 2)).toPoint()));
        QTest::qWait(80);
        GetCursorPos(&point);
        if (GetForegroundWindow() != playerHandle || WindowFromPoint(point) != toolbarHandle)
            QSKIP("The toolbar input fixture is occluded; no toolbar mouse input was injected.");
        QCOMPARE(SendInput(2, inputs, sizeof(INPUT)), UINT(2));
        QTRY_VERIFY(popup->isVisible());
        QCOMPARE(GetForegroundWindow(), playerHandle);
        backend.stop();
#endif
    }

    void nativePanelAndDialogPolicy() {
#if defined(Q_OS_WIN)
        QTemporaryDir directory;
        AppController backend(directory.path(), nullptr, {}, directory.path());
        QQmlApplicationEngine engine;
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* panel = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(panel);
        auto* overlay = panel->findChild<QQuickWindow*>("danmakuOverlay");
        auto* toolbar = panel->findChild<QQuickWindow*>("danmakuToolbarWindow");
        QVERIFY(panel && overlay && toolbar);
        QVERIFY(!overlay->transientParent());
        QCOMPARE(toolbar->transientParent(), overlay);
        panel->show();
        panel->requestActivate();
        QTRY_VERIFY(panel->isActive());
        QTest::qWait(200);
        const auto owner = reinterpret_cast<HWND>(panel->winId());
        if (GetForegroundWindow() != owner)
            QSKIP("Windows foreground was not granted to the test panel; native owner policy needs an available desktop.");
        backend.beginDemo(50);
        QTRY_VERIFY(overlay->isExposed());
        QVERIFY(!backend.toolbarPresentationAllowed());
        QVERIFY(!toolbar->isVisible());
        const auto nativeOverlay = reinterpret_cast<HWND>(overlay->winId());
        const auto above = [](HWND upper, HWND lower) {
            for (auto current = GetWindow(lower, GW_HWNDPREV); current; current = GetWindow(current, GW_HWNDPREV))
                if (current == upper) return true;
            return false;
        };
        auto* settings = qobject_cast<SettingsStore*>(backend.settings());
        for (int strategy = 0; strategy < 4; ++strategy) {
            QVERIFY(settings->setValue("onTop", strategy));
            QTest::qWait(1100);
            QVERIFY(above(owner, nativeOverlay));
            QCOMPARE(bool(GetWindowLongPtrW(owner, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QCOMPARE(bool(GetWindowLongPtrW(nativeOverlay, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QVERIFY(!panel->flags().testFlag(Qt::WindowStaysOnTopHint));
            QCOMPARE(GetForegroundWindow(), owner);
        }

        WNDCLASSW probeClass{};
        probeClass.lpfnWndProc = DefWindowProcW;
        probeClass.hInstance = GetModuleHandleW(nullptr);
        probeClass.lpszClassName = L"DanmaXToolbarOwnerProbe";
        QVERIFY(RegisterClassW(&probeClass) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
        struct NativeDialog {
            HWND handle;
            ~NativeDialog() { if (handle) DestroyWindow(handle); }
        } dialog{CreateWindowExW(WS_EX_TOOLWINDOW, probeClass.lpszClassName, L"Toolbar policy native owner probe",
                               WS_POPUP | WS_CAPTION | WS_VISIBLE, 200, 200, 320, 160, owner, nullptr,
                               probeClass.hInstance, nullptr)};
        QVERIFY(dialog.handle);
        SetForegroundWindow(dialog.handle);
        QTRY_VERIFY(GetForegroundWindow() == dialog.handle);
        QVERIFY(MediaMonitor::windowOrOwnerForeground(panel->winId()));
        QTest::qWait(100);
        QVERIFY(backend.overlayVisible());
        QVERIFY(!toolbar->isVisible());
        QVERIFY(above(dialog.handle, owner));
        QVERIFY(above(owner, nativeOverlay));
        NativeDialog nested{CreateWindowExW(WS_EX_TOOLWINDOW, probeClass.lpszClassName, L"Nested owner probe",
            WS_POPUP | WS_CAPTION | WS_VISIBLE, 240, 240, 280, 120, dialog.handle, nullptr,
            probeClass.hInstance, nullptr)};
        QVERIFY(nested.handle);
        SetForegroundWindow(nested.handle);
        QTRY_VERIFY(GetForegroundWindow() == nested.handle);
        QTest::qWait(1100);
        QVERIFY(MediaMonitor::windowOrOwnerForeground(panel->winId()));
        QVERIFY(above(nested.handle, dialog.handle));
        QVERIFY(above(dialog.handle, owner));
        QVERIFY(above(owner, nativeOverlay));
        ShowWindow(nested.handle, SW_HIDE);
        ShowWindow(dialog.handle, SW_HIDE);
        panel->showMinimized();
        QTRY_VERIFY(backend.overlayVisible());
        QTRY_VERIFY(backend.position() > 0);
        QTRY_VERIFY(!(GetWindowLongPtrW(owner, GWL_EXSTYLE) & WS_EX_TOPMOST));
        QVERIFY(GetWindowLongPtrW(nativeOverlay, GWL_EXSTYLE) & WS_EX_TRANSPARENT);
        QVERIFY(GetWindowLongPtrW(nativeOverlay, GWL_EXSTYLE) & WS_EX_NOACTIVATE);
        QVERIFY(!GetWindow(nativeOverlay, GW_OWNER));
        panel->showNormal();
        panel->requestActivate();
        QTRY_VERIFY(GetForegroundWindow() == owner);
        QTRY_VERIFY(GetWindowLongPtrW(owner, GWL_EXSTYLE) & WS_EX_TOPMOST);
#if defined(QT_DEBUG)
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/panel-layer/debug");
#else
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/panel-layer/release");
#endif
        QDir().mkpath(evidence);
        QVERIFY(panel->grabWindow().save(evidence + "/panel.png"));
        QVERIFY(overlay->grabWindow().save(evidence + "/overlay.png"));
        backend.writeReport(evidence + "/context.json");
        backend.stop();
        QVERIFY(!(GetWindowLongPtrW(owner, GWL_EXSTYLE) & WS_EX_TOPMOST));
#endif
    }

    void toolbarAutoHideWorkflow() {
        QTemporaryDir directory;
        // Keep state/animation checks independent of concurrent desktop mouse movement.
        // nativeToolbarMousePolicy separately exercises QCursor and Windows hit testing.
        QPoint sensedCursor;
        bool panelOperating = false;
        AppController backend(directory.path(), nullptr, {}, directory.path(),
                              [&](QWindow*) { return panelOperating; }, [&] { return sensedCursor; });
        auto* settings = qobject_cast<SettingsStore*>(backend.settings());
        QVERIFY(settings->setValue("onTop", 1));
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        QQmlComponent overlayComponent(&engine, QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/OverlayWindow.qml")));
        QVERIFY2(overlayComponent.isReady(), qPrintable(overlayComponent.errorString()));
        std::unique_ptr<QObject> overlayObject(overlayComponent.createWithInitialProperties(
            {{"backend", QVariant::fromValue(&backend)}}));
        auto* overlayWindow = qobject_cast<QQuickWindow*>(overlayObject.get());
        QVERIFY(overlayWindow);
        auto& overlay = *overlayWindow;
        auto* canvas = qobject_cast<DanmakuItem*>(overlay.property("renderer").value<QObject*>());
        QVERIFY(canvas);
        auto& renderer = *canvas;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/ToolbarWindow.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> object(component.createWithInitialProperties(
            {{"backend", QVariant::fromValue(&backend)}, {"overlayWindow", QVariant::fromValue(overlayObject.get())}}));
        auto* window = qobject_cast<QQuickWindow*>(object.get());
        QVERIFY(window);
        auto* bar = window->findChild<QQuickItem*>("danmakuToolbar");
        auto* opacityButton = window->findChild<QQuickItem*>("toolbarOpacityButton");
        auto* pinButton = window->findChild<QQuickItem*>("toolbarPinButton");
        auto* pinTooltip = window->findChild<QObject*>("toolbarPinTooltip");
        auto* popup = window->findChild<QQuickItem*>("toolbarOpacityPopup");
        auto* slider = window->findChild<QQuickItem*>("toolbarOpacitySlider");
        QVERIFY(bar && opacityButton && pinButton && pinTooltip && popup && slider);
        QCOMPARE(window->transientParent(), &overlay);
        QWindow panel;
        panel.setGeometry(200, 150, 500, 400);
        panel.show();
        backend.attach(&renderer, &overlay, window, &panel);
        connect(&backend, &AppController::stateChanged, &overlay,
                [&] { overlay.setVisible(backend.overlayVisible()); });
        QQuickWindow player;
        player.setFlags(Qt::Window | Qt::FramelessWindowHint);
        player.setGeometry(overlay.geometry());
        player.setColor(QColor("#303030"));
        player.show();
        player.requestActivate();
        QTRY_VERIFY(player.isActive());
        const auto originalCursor = QCursor::pos();
        struct CursorRestore { QPoint point; ~CursorRestore() { QCursor::setPos(point); } } restore{originalCursor};
        auto outside = [&] { sensedCursor = overlay.geometry().center() + QPoint(0, 150); };
        auto enter = [&] { sensedCursor = QPoint(overlay.x() + overlay.width() / 2, overlay.y() + 2); };
        auto click = [&](QQuickItem* item) {
            QSignalSpy clicked(item, SIGNAL(clicked()));
            const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
            sensedCursor = window->mapToGlobal(point);
            QCursor::setPos(sensedCursor);
            QTest::mouseMove(window, point);
            QTest::qWait(80);
            QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_VERIFY(item->property("pressed").toBool());
            QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, point);
            QTRY_COMPARE(clicked.count(), 1);
        };
        outside();
        backend.beginDemo(50);
        QTRY_VERIFY(overlay.isVisible());
        QTRY_VERIFY(backend.toolbarPointer()->property("enabled").toBool());
        QVERIFY(!window->isVisible());
        for (int cycle = 0; cycle < 3; ++cycle) {
            enter();
            QTRY_VERIFY(backend.toolbarPointer()->property("hovered").toBool());
            QVERIFY(bar->property("presentationAllowed").toBool());
            QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
            QVERIFY(window->isVisible());
            const auto size = window->size();
            QVERIFY(size.width() > 80 && size.height() > 10);
            outside();
            QTRY_COMPARE(window->property("phase").toString(), QString("closing"));
            QCOMPARE(window->size(), size);
            QVERIFY(window->isVisible());
            if (cycle == 0) {
                enter();
                QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
                outside();
            } else if (cycle == 1) {
                panelOperating = true;
                QTRY_VERIFY(!backend.toolbarPresentationAllowed());
                QCOMPARE(window->property("phase").toString(), QString("hidden"));
                QVERIFY(!window->isVisible());
                panelOperating = false;
                QTRY_VERIFY(backend.toolbarPresentationAllowed());
            }
            QTRY_VERIFY(!window->isVisible());
            QCOMPARE(window->property("phase").toString(), QString("hidden"));
        }
        enter();
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        const auto pinPoint = pinButton->mapToScene(QPointF(pinButton->width() / 2, pinButton->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, pinPoint);
        QVERIFY(bar->property("interactionHeld").toBool());
        outside();
        QTest::mouseMove(window, QPoint(-100, 200));
        QTest::qWait(1200);
        QVERIFY(window->isVisible());
        panelOperating = true;
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(!backend.toolbarPresentationAllowed());
        QVERIFY(overlay.isVisible());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(-100, 200));
        panelOperating = false;
        QTRY_VERIFY(backend.toolbarPresentationAllowed());
        QTRY_VERIFY(!window->isVisible());
        QVERIFY(!bar->property("pinned").toBool());
        enter();
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        click(opacityButton);
        QVERIFY(popup->isVisible());
        // The transparent gap between card and popover must also retain the toolbar.
        sensedCursor = QPoint(window->x() + window->width() / 2, window->y() + 48);
        QTest::qWait(1200);
        QVERIFY(popup->isVisible());
        auto* handle = qobject_cast<QQuickItem*>(slider->property("handle").value<QObject*>());
        QVERIFY(handle);
        const auto thumb = handle->mapToScene(QPointF(handle->width() / 2, handle->height() / 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, thumb);
        QTRY_VERIFY(slider->property("pressed").toBool());
        panelOperating = true;
        QTRY_VERIFY(!backend.toolbarPresentationAllowed());
        QVERIFY(overlay.isVisible());
        QVERIFY(!window->isVisible());
        QCOMPARE(window->property("phase").toString(), QString("hidden"));
        QVERIFY(!popup->isVisible());
        QVERIFY(!backend.toolbarPointer()->property("enabled").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, thumb);
        panelOperating = false;
        outside();
        QTRY_VERIFY(backend.toolbarPresentationAllowed());
        QVERIFY(!window->isVisible());
        enter();
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        click(opacityButton);
        QVERIFY(popup->isVisible());
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, thumb);
        QTRY_VERIFY(slider->property("pressed").toBool());
        outside();
        QTest::mouseMove(window, QPoint(-100, 200));
        QTest::qWait(1200);
        QVERIFY(popup->isVisible());
        QVERIFY(window->isVisible());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, QPoint(-100, 200));
        QTRY_VERIFY(!popup->isVisible());
        QTRY_VERIFY(!window->isVisible());

        enter();
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        click(pinButton);
        QVERIFY(bar->property("pinned").toBool());
        for (int strategy = 0; strategy < 4; ++strategy) {
            QVERIFY(settings->setValue("onTop", strategy));
            QTest::qWait(1100);
            QCOMPARE(overlay.flags().testFlag(Qt::WindowStaysOnTopHint), strategy > 0);
            QCOMPARE(window->flags().testFlag(Qt::WindowStaysOnTopHint), strategy > 0);
#if defined(Q_OS_WIN)
            const auto nativeToolbar = reinterpret_cast<HWND>(window->winId());
            const auto nativeOverlay = reinterpret_cast<HWND>(overlay.winId());
            QVERIFY(GetWindowLongPtrW(nativeToolbar, GWL_EXSTYLE) & WS_EX_NOACTIVATE);
            QCOMPARE(bool(GetWindowLongPtrW(nativeToolbar, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QCOMPARE(bool(GetWindowLongPtrW(nativeOverlay, GWL_EXSTYLE) & WS_EX_TOPMOST), strategy > 0);
            QVERIFY(GetWindow(nativeToolbar, GW_OWNER) == reinterpret_cast<HWND>(overlay.winId()));
#endif
        }
#if defined(QT_DEBUG)
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/toolbar/debug");
#else
        const auto evidence = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/toolbar/release");
#endif
        const auto scaledEvidence = evidence + QStringLiteral("-dpr%1").arg(overlay.devicePixelRatio());
        QDir().mkpath(scaledEvidence);
        QVERIFY(window->grabWindow().save(scaledEvidence + "/pinned.png"));
        backend.writeReport(scaledEvidence + "/context.json");
        click(opacityButton);
        QVERIFY(popup->isVisible());
        const auto scheme = QGuiApplication::styleHints()->colorScheme();
        for (auto theme : {Qt::ColorScheme::Light, Qt::ColorScheme::Dark}) {
            QGuiApplication::styleHints()->setColorScheme(theme);
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(scaledEvidence +
                (theme == Qt::ColorScheme::Light ? "/popover-light.png" : "/popover-dark.png")));
        }
        QGuiApplication::styleHints()->setColorScheme(scheme);
        outside();
        QTRY_VERIFY(!popup->isVisible());
        QVERIFY(window->isVisible());
        QVERIFY(bar->property("expanded").toBool());
        auto hoverPinTooltip = [&] {
            const auto point = pinButton->mapToScene(QPointF(pinButton->width() / 2, pinButton->height() / 2)).toPoint();
            sensedCursor = window->mapToGlobal(point);
            QCursor::setPos(sensedCursor);
            QTest::mouseMove(window, point);
            QTRY_VERIFY(pinTooltip->property("opened").toBool());
            auto* content = qobject_cast<QQuickItem*>(pinTooltip->property("contentItem").value<QObject*>());
            QVERIFY(content && content->window() && content->window() != window);
            QCOMPARE(content->window()->type(), Qt::ToolTip);
            QVERIFY(!content->window()->isActive());
            const QRect buttonRect(sensedCursor - QPoint(qRound(pinButton->width() / 2), qRound(pinButton->height() / 2)),
                                   QSize(qRound(pinButton->width()), qRound(pinButton->height())));
            QVERIFY(!content->window()->geometry().intersects(buttonRect));
            QVERIFY(content->window()->grabWindow().save(scaledEvidence + "/pin-tooltip.png"));
        };
        hoverPinTooltip();
        panelOperating = true;
        QTRY_VERIFY(!window->isVisible());
        QTRY_VERIFY(!pinTooltip->property("visible").toBool());
        QVERIFY(bar->property("pinned").toBool());
        enter();
        QTest::qWait(1100);
        QVERIFY(!window->isVisible());
        QVERIFY(overlay.isVisible());
        QVERIFY(!backend.toolbarPointer()->property("enabled").toBool());
        outside();
        panelOperating = false;
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        click(opacityButton);
        QVERIFY(popup->isVisible());
        backend.stop();
        QTRY_VERIFY(!window->isVisible());
        QCOMPARE(window->property("phase").toString(), QString("hidden"));
        QVERIFY(!popup->isVisible());
        QVERIFY(!bar->property("autoExpanded").toBool());
        QVERIFY(bar->property("pinned").toBool());
        outside();
        backend.beginDemo(50);
        QTRY_COMPARE(window->property("phase").toString(), QString("shown"));
        hoverPinTooltip();
        click(pinButton);
        QVERIFY(!bar->property("pinned").toBool());
        outside();
        QTRY_VERIFY(!window->isVisible());
        backend.stop();
        QCOMPARE(warnings.count(), 0);
    }

    void backdropPolicyAndPersistence() {
        using Backdrop = WindowBackdropController;
        QVERIFY(Backdrop::fallback(true, true, true, false, true, false).isEmpty());
        QVERIFY(!Backdrop::fallback(false, true, true, false, true, false).isEmpty());
        QVERIFY(!Backdrop::fallback(true, false, true, false, true, false).isEmpty());
        QVERIFY(!Backdrop::fallback(true, true, false, false, true, false).isEmpty());
        QVERIFY(!Backdrop::fallback(true, true, true, true, true, false).isEmpty());
        QVERIFY(!Backdrop::fallback(true, true, true, false, false, false).isEmpty());
        QVERIFY(!Backdrop::fallback(true, true, true, false, true, true).isEmpty());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        {
            SettingsStore settings(directory.path());
            QVERIFY(settings.values().value("micaEnabled").toBool());
            QVERIFY(settings.setValue("micaEnabled", false));
            QVERIFY(!settings.setValue("micaEnabled", "false"));
        }
        SettingsStore restored(directory.path());
        QVERIFY(!restored.values().value("micaEnabled").toBool());
        QVERIFY(restored.reset());
        QVERIFY(restored.values().value("micaEnabled").toBool());
    }
    void backdropSurfaceLifecycle() {
        QTemporaryDir directory;
        SettingsStore settings(directory.path());
        WindowBackdropController backdrop(&settings, nullptr);
        {
            QQuickWindow window;
            window.setColor(Qt::transparent);
            backdrop.attach(&window);
            // ui_workflow uses the software backend: no DWM mutation is allowed.
            QVERIFY(!backdrop.effectiveMica());
            QVERIFY(!backdrop.fallbackReason().isEmpty());
            window.destroy();
            QVERIFY(!backdrop.effectiveMica());
            window.create();
            QCoreApplication::processEvents();
            QVERIFY(!backdrop.effectiveMica());
            QVERIFY(settings.setValue("micaEnabled", false));
            // Destroy the target with a queued refresh outstanding.
        }
        QCoreApplication::processEvents();
        QVERIFY(!backdrop.effectiveMica());
    }
    void popupGeometryAndInput() {
        const QString output = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/scroll-popup")
            + qEnvironmentVariable("DANMAKU_UI_CAPTURE_SUFFIX");
        QDir().mkpath(output);
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.loadData(R"(
import QtQuick
import QtQuick.Controls.FluentWinUI3
import "components"
ApplicationWindow {
    id: root
    visible: true
    width: 1080; height: 760
    property real anchorY: 40
    AppComboBox {
        id: choice; objectName: "probeChoice"
        x: 24; y: root.anchorY; width: root.width - 48
        model: []; textRole: "title"; valueRole: "id"
        Accessible.name: "剧集"
    }
    Button { x: 24; y: 0; text: "下一项" }
}
)", QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/PopupProbe.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        auto* combo = window->findChild<QQuickItem*>("probeChoice");
        QVERIFY(combo);
        auto* popup = combo->property("popup").value<QObject*>();
        auto* list = qobject_cast<QQuickItem*>(combo->property("popupList").value<QObject*>());
        QVERIFY(popup && list);
        QSignalSpy hiding(popup, SIGNAL(aboutToHide()));
        QSignalSpy opened(popup, SIGNAL(opened()));
        auto* bar = qobject_cast<QQuickItem*>(combo->property("listScrollBar").value<QObject*>());
        QVERIFY(bar);
        auto bounds = [&] {
            const auto point = list->mapToScene(QPointF());
            QVERIFY(point.x() >= 7 && point.y() >= 7);
            QVERIFY(point.x() + list->width() <= window->width() - 7);
            QVERIFY(point.y() + list->height() <= window->height() - 7);
            QVERIFY(popup->property("height").toReal() <= combo->property("popupLimit").toReal() + 1);
        };
        QVariantList rows;
        for (int i = 0; i < 60; ++i)
            rows.append(QVariantMap{{"id", i}, {"title", QString::fromUtf8("第 %1 话 很长的中文剧集名称，用于验证文字省略和完整提示 Electro Master").arg(i + 1)}});
        combo->setProperty("model", rows);
        combo->setProperty("currentIndex", 59);
        // Isolate keyboard selection from synthetic/native pointer motion during
        // theme changes and high-DPI window repositioning.
        combo->setProperty("hoverEnabled", false);
        // Keep the pointer outside the popup while testing keyboard selection;
        // a stationary pointer over a row can legitimately change its highlight.
        QCursor::setPos(window->mapToGlobal(QPoint(1, 1)));
        QTest::mouseMove(window, QPoint(1, 1));
        QTest::qWait(50);
        combo->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(popup->property("opened").toBool());
        bounds();
        QVERIFY(bar->isVisible());
        QVERIFY(list->property("contentY").toReal() > 0);
        auto* current = qobject_cast<QQuickItem*>(list->property("currentItem").value<QObject*>());
        QVERIFY(current);
        QVERIFY(current->width() <= list->width());
        QVERIFY(current->y() + current->height() <= list->property("contentY").toReal() + list->height() + 1);
        QVERIFY(window->grabWindow().save(output + "/long-list.png"));
        auto* surface = popup->property("background").value<QObject*>();
        QVERIFY(surface);
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
        QTest::qWait(80);
        QVERIFY(window->grabWindow().save(output + "/long-list-light.png"));
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
        QTest::qWait(80);
        QVERIFY(window->grabWindow().save(output + "/long-list-dark.png"));
        QCOMPARE(combo->property("highlightedIndex").toInt(), 59);
        QTest::keyClick(window, Qt::Key_Up);
        QCOMPARE(combo->property("highlightedIndex").toInt(), 58);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(combo->property("currentIndex").toInt(), 58);
        QVERIFY(combo->hasActiveFocus());
        combo->setProperty("hoverEnabled", true);
        const auto openAfterResize = [&] {
            // Native resize/exposure and activation are asynchronous. Open only after
            // one frame of the resized window, with the same focus a user click supplies.
            QSignalSpy frames(window, &QQuickWindow::frameSwapped);
            window->requestActivate();
            QVERIFY(QTest::qWaitForWindowActive(window));
            window->update();
            QTRY_VERIFY(!frames.isEmpty());
            combo->forceActiveFocus();
            QTRY_VERIFY(combo->hasActiveFocus());
            QVERIFY(QMetaObject::invokeMethod(popup, "open"));
        };
        for (const int width : {1080, 520}) {
            window->resize(width, 480);
            window->setProperty("anchorY", 400);
            openAfterResize();
            QTRY_VERIFY2(popup->property("opened").toBool(), qPrintable(QString("width=%1 visible=%2 hides=%3 opens=%4 active=%5 height=%6")
                .arg(width).arg(popup->property("visible").toBool()).arg(hiding.count()).arg(opened.count())
                .arg(window->isActive()).arg(popup->property("height").toReal())));
            bounds();
            QVERIFY(popup->property("y").toReal() < 0);
            QVERIFY(window->grabWindow().save(output + QString("/upward-%1.png").arg(width)));
            window->resize(width, 320);
            QTest::qWait(80);
            bounds();
            QTest::keyClick(window, Qt::Key_Escape);
            QTRY_VERIFY(!popup->property("visible").toBool());
        }
        window->resize(520, 480);
        window->setProperty("anchorY", 40);
        combo->setProperty("model", QVariantList{rows.first(), rows.at(1)});
        combo->setProperty("currentIndex", 0);
        openAfterResize();
        QTRY_VERIFY(popup->property("opened").toBool());
        bounds();
        QTRY_VERIFY(bar->property("size").toReal() >= 1.0);
        combo->setProperty("model", rows);
        QTRY_VERIFY(bar->property("size").toReal() < 1.0);
        bounds();
        // Drag the real standard scrollbar, then wheel the original popup ListView.
        const QPoint from = bar->mapToScene(QPointF(bar->width() / 2, bar->property("topPadding").toReal() + 3)).toPoint();
        const QPoint to = bar->mapToScene(QPointF(bar->width() / 2, bar->height() - bar->property("bottomPadding").toReal() - 2)).toPoint();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(window, to, 100);
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
        QTRY_VERIFY(list->property("contentY").toReal() > 0);
        const qreal beforeWheel = list->property("contentY").toReal();
        const auto local = list->mapToScene(QPointF(40, 40));
        QWheelEvent wheel(local, window->mapToGlobal(local.toPoint()), QPoint(), QPoint(0, 120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(window, &wheel);
        QTRY_VERIFY(list->property("contentY").toReal() < beforeWheel);
        combo->setProperty("model", QVariantList{});
        QTest::qWait(80);
        QTRY_COMPARE(list->property("contentHeight").toReal(), 0.0);
        QVERIFY(bar->height() <= 0 || bar->property("size").toReal() >= 1.0);
        QTest::keyClick(window, Qt::Key_Escape);
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(!combo->hasActiveFocus());
        QCOMPARE(warnings.count(), 0);
    }
    void workflow() {
        const QString output = QStringLiteral(DANMAKU_SOURCE_DIR "/out/validation/ui-refresh")
            + qEnvironmentVariable("DANMAKU_UI_CAPTURE_SUFFIX");
        QDir().mkpath(output);
        QTemporaryDir temporary(output + "/session-XXXXXX");
        QVERIFY(temporary.isValid());
        AppController backend(temporary.path() + "/data", nullptr, {}, temporary.path() + "/cache");
        auto* settings = qobject_cast<SettingsStore*>(backend.settings());
        auto* logs = qobject_cast<LogModel*>(backend.logs());
        settings->setValue("theme", "dark");
        QQmlApplicationEngine engine;
        QSignalSpy warnings(&engine, &QQmlApplicationEngine::warnings);
        engine.setInitialProperties({{"backend", QVariant::fromValue(&backend)}});
        engine.load(QUrl::fromLocalFile(QStringLiteral(DANMAKU_UI_DIR "/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QVERIFY(window);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        // Repeater delegates belong to the visual tree, not necessarily the QObject tree.
        std::function<QQuickItem*(QQuickItem*, const QString&)> findVisual = [&](QQuickItem* parent, const QString& name) -> QQuickItem* {
            if (parent->objectName() == name) return parent;
            for (auto* child : parent->childItems()) if (auto* match = findVisual(child, name)) return match;
            return nullptr;
        };
        auto find = [&](const char* name) { return findVisual(window->contentItem(), QString::fromLatin1(name)); };
        auto activate = [&](QQuickItem* item) {
            QVERIFY(item);
            item->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Space);
            QTest::qWait(50);
        };
        activate(find("navigation2"));
        QCOMPARE(window->property("selectedPage").toInt(), 2);
        activate(find("micaSwitch"));
        QVERIFY(!settings->values().value("micaEnabled").toBool());
        activate(find("micaSwitch"));
        QVERIFY(settings->values().value("micaEnabled").toBool());
        activate(find("navigation3"));
        QCOMPARE(window->property("selectedPage").toInt(), 3);
        QVERIFY(find("reportBugCard"));
        activate(find("navigation1"));
        QCOMPARE(window->property("selectedPage").toInt(), 1);
        activate(find("navigation0"));
        QCOMPARE(window->property("selectedPage").toInt(), 0);
        auto* start = find("startPlayback");
        QVERIFY(start);
        auto* sourceTab = find("sourceLocalTab");
        QVERIFY(sourceTab);
        auto* tabLabel = sourceTab->property("contentItem").value<QObject*>();
        QVERIFY(tabLabel);
        QVERIFY(tabLabel->property("color").value<QColor>().alpha() > 0);
        QVERIFY(!start->isEnabled());
        QVERIFY(!find("stopPlayback")->isVisible());
        auto* disclosure = find("sessionDisclosure");
        QVERIFY(disclosure);
        disclosure->setProperty("expanded", true);
        find("sessionIdInput")->forceActiveFocus();
        disclosure->setProperty("expanded", false);
        auto* disclosureButton = qobject_cast<QQuickItem*>(disclosure->property("toggleButton").value<QObject*>());
        QVERIFY(disclosureButton && disclosureButton->hasActiveFocus());
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(output + "/empty-dark.png"));
        activate(find("manualMode"));
        QSignalSpy loaded(&backend, &AppController::loadCompleted);
        backend.loadFile(QStringLiteral(DANMAKU_SOURCE_DIR "/tests/fixtures/sample.xml"));
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        QVERIFY(backend.total() > 0);
        QTRY_VERIFY(start->isEnabled());
        activate(start);
        QVERIFY(backend.running());
        QVERIFY(backend.manualMode());
        QVERIFY(!find("manualMode")->isEnabled());
        activate(find("pausePlayback"));
        QVERIFY(!backend.playing());
        const auto paused = backend.position();
        QTest::qWait(80);
        QCOMPARE(backend.position(), paused);
        activate(find("pausePlayback"));
        QVERIFY(backend.playing());
        activate(find("stopPlayback"));
        QVERIFY(!backend.running());
        QCOMPARE(backend.total(), 0);
        activate(find("syncMode"));
        backend.selectSession("ui-test-session");
        loaded.clear();
        backend.loadFile(QStringLiteral(DANMAKU_SOURCE_DIR "/tests/fixtures/sample.xml"));
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        activate(start);
        QVERIFY(backend.running());
        QVERIFY(!backend.manualMode());
        QVERIFY(!find("pausePlayback")->isVisible());
        loaded.clear();
        backend.loadFile(output + "/does-not-exist.xml");
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        // Local XML loading intentionally unloads before preparation; preserve that contract.
        QCOMPARE(backend.total(), 0);
        QVERIFY(!backend.running());
        QVERIFY(!backend.error().isEmpty());
        backend.clearError();
        backend.stop();
        QFile emptyFile(temporary.filePath("empty.xml"));
        QVERIFY(emptyFile.open(QIODevice::WriteOnly));
        emptyFile.write("<?xml version=\"1.0\" encoding=\"UTF-8\"?><i></i>");
        emptyFile.close();
        loaded.clear();
        backend.loadFile(emptyFile.fileName());
        QTRY_VERIFY_WITH_TIMEOUT(!loaded.isEmpty(), 10000);
        QCOMPARE(backend.total(), 0);
        QVERIFY(!backend.error().isEmpty());
        QVERIFY(!start->isEnabled());
        backend.clearError();
        auto* tabs = find("danmakuSourceTabs");
        tabs->setProperty("currentIndex", 2);
        window->setProperty("selectedPage", 2);
        QTest::qWait(50);
        window->setProperty("selectedPage", 0);
        QCOMPARE(tabs->property("currentIndex").toInt(), 2);
        tabs->setProperty("currentIndex", 1);
        auto* player = tabs;
        while (player && player->metaObject()->indexOfSignal("openSettings()") < 0) player = player->parentItem();
        QVERIFY(player);
        QVERIFY(QMetaObject::invokeMethod(player, "openSettings"));
        QTRY_COMPARE(window->property("selectedPage").toInt(), 2);
        QTRY_VERIFY(find("danmakuServerInput"));
        QTRY_VERIFY(find("danmakuServerInput")->hasActiveFocus());
        const auto serverPosition = find("danmakuServerInput")->mapToScene(QPointF());
        QVERIFY(serverPosition.y() >= 0 && serverPosition.y() < window->height());
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(!find("danmakuServerInput")->hasActiveFocus());
        const auto defaults = settings->values()["danmakuServers"].toStringList();
        activate(find("addDanmakuServer"));
        QTRY_VERIFY(find("danmakuServerInput1"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList(), defaults);
        auto editServer = [&](const char* name, const QString& value) {
            auto* input = find(name); QVERIFY(input);
            input->setProperty("text", value);
            QVERIFY(QMetaObject::invokeMethod(input, "editingFinished"));
        };
        editServer("danmakuServerInput1", "bad address");
        QCOMPARE(find("danmakuServerInput1")->property("text").toString(), QString("bad address"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList(), defaults);
        editServer("danmakuServerInput1", defaults.first());
        QCOMPARE(settings->values()["danmakuServers"].toStringList(), defaults);
        editServer("danmakuServerInput1", "https://backup.test");
        activate(find("saveServersBtn"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList().size(), 2);
        auto* onlineFrame = find("settingsPage");
        QVERIFY(onlineFrame);
        QVERIFY(QMetaObject::invokeMethod(onlineFrame, "focusOnlineSettings"));
        for (const auto& theme : {QString("light"), QString("dark")}) {
            settings->setValue("theme", theme);
            window->resize(520, 640);
            QVERIFY(QMetaObject::invokeMethod(onlineFrame, "reveal", Q_ARG(QVariant, QVariant::fromValue(find("danmakuServerInput")))));
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(output + "/online-servers-" + theme + ".png"));
        }
        activate(find("serverUp1"));
        activate(find("saveServersBtn"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList().first(), QString("https://backup.test"));
        QVERIFY(QMetaObject::invokeMethod(onlineFrame, "focusOnlineSettings"));
        activate(find("serverDown0"));
        activate(find("saveServersBtn"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList().first(), defaults.first());
        QVERIFY(QMetaObject::invokeMethod(onlineFrame, "focusOnlineSettings"));
        activate(find("serverRemove1"));
        activate(find("serverRemove0"));
        activate(find("saveServersBtn"));
        QVERIFY(settings->values()["danmakuServers"].toStringList().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(onlineFrame, "focusOnlineSettings"));
        QVERIFY(find("addDanmakuServer")->hasActiveFocus());
        QVERIFY(settings->setDanmakuServers(defaults));
        QVERIFY(QMetaObject::invokeMethod(onlineFrame, "focusOnlineSettings"));
        QTRY_VERIFY(find("danmakuServerInput"));
        activate(find("cancelServersBtn"));
        window->setProperty("selectedPage", 1);
        logs->clear();
        QTRY_COMPARE(find("logEmptyState")->property("text").toString(), QString::fromUtf8("暂无日志"));
        logs->append("INFO", QString::fromUtf8("测试消息：这是一条用于检查换行和筛选状态的中文日志。"));
        find("logFilter")->setProperty("currentIndex", 2);
        QTRY_VERIFY(find("logEmptyState")->isVisible());
        QCOMPARE(find("logEmptyState")->property("text").toString(), QString::fromUtf8("当前筛选无结果"));
        logs->append("ERROR", "test error");
        QTRY_VERIFY(!find("logEmptyState")->isVisible());
        QCOMPARE(logs->countForLevel("ERROR"), 1);
        find("logFilter")->setProperty("currentIndex", 0);
        for (const auto& theme : {QString("light"), QString("dark")}) {
            settings->setValue("theme", theme);
            for (int width : {520, 800, 1080}) {
                window->resize(width, width == 520 ? 480 : 760);
                for (int page = 0; page < 3; ++page) {
                    QVERIFY(QMetaObject::invokeMethod(window, "navigate", Q_ARG(QVariant, page)));
                    QTest::qWait(80);
                    QVERIFY(window->grabWindow().save(output + QString("/page-%1-%2-%3.png").arg(page).arg(theme).arg(width)));
                }
                auto* font = find("fontFamilyCombo");
                auto* frame = font->parentItem();
                while (frame && !frame->property("bodyItem").isValid()) frame = frame->parentItem();
                QVERIFY(frame);
                auto* body = qobject_cast<QQuickItem*>(frame->property("bodyItem").value<QObject*>());
                auto* bar = qobject_cast<QQuickItem*>(frame->property("verticalBar").value<QObject*>());
                auto* flick = frame->property("contentItem").value<QObject*>();
                QVERIFY(body && bar && flick);
                const auto bodyRight = body->mapToScene(QPointF(body->width(), 0)).x();
                const auto barLeft = bar->mapToScene(QPointF()).x();
                QVERIFY(bodyRight + 7 <= barLeft);
                QCOMPARE(qRound(frame->width() - bar->x() - bar->width()), 8);
                const auto bodyWidth = body->width();
                for (qreal position : {0.0, 0.4, 1.0}) {
                    bar->setProperty("position", std::min(position, 1 - bar->property("size").toReal()));
                    QTest::qWait(50);
                    QCOMPARE(body->width(), bodyWidth);
                    QVERIFY(flick->property("contentWidth").toReal() <= frame->width());
                    QVERIFY(window->grabWindow().save(output + QString("/scroll-%1-%2-%3.png").arg(theme).arg(width).arg(position)));
                }
                QVERIFY(flick->property("atYEnd").toBool());
                bar->setProperty("position", 0.0);
                QTest::qWait(50);
                const QPoint dragStart = bar->mapToScene(QPointF(bar->width() / 2, bar->property("topPadding").toReal() + 3)).toPoint();
                const QPoint dragEnd = bar->mapToScene(QPointF(bar->width() / 2, bar->height() - bar->property("bottomPadding").toReal() - 2)).toPoint();
                QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, dragStart);
                QTest::mouseMove(window, dragEnd, 80);
                QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, dragEnd);
                QTRY_VERIFY(flick->property("atYEnd").toBool());
                QVERIFY(QMetaObject::invokeMethod(frame, "reveal", Q_ARG(QVariant, QVariant::fromValue(font))));
                font->forceActiveFocus();
                QTest::qWait(80);
                QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
                    font->mapToScene(QPointF(font->width() - 16, font->height() / 2)).toPoint());
                auto* popup = font->property("popup").value<QObject*>();
                QTRY_VERIFY(popup->property("opened").toBool());
                QVERIFY(window->grabWindow().save(output + QString("/font-popup-%1-%2.png").arg(theme).arg(width)));
                QTest::keyClick(window, Qt::Key_Escape);
                QTRY_VERIFY(!popup->property("visible").toBool());
                font->forceActiveFocus();
                QTest::keyClick(window, Qt::Key_F4);
                QTRY_VERIFY(popup->property("opened").toBool());
                QTest::keyClick(window, Qt::Key_Escape);
                QTRY_VERIFY(!popup->property("visible").toBool());
                // Lower settings groups must be scrolled into view, beyond
                // initial page snapshots. Exercise the standard switch.
                for (const char* name : {"cacheGroup", "diagnosticsGroup"}) {
                    auto* group = find(name);
                    QVERIFY(group);
                    QTest::qWait(50);
                    QVERIFY(QMetaObject::invokeMethod(frame, "reveal", Q_ARG(QVariant, QVariant::fromValue(group))));
                    QTest::qWait(80);
                    group->setFocus(false);
                    QVERIFY(window->grabWindow().save(output + QString("/group-%1-%2-%3.png").arg(name).arg(theme).arg(width)));
                    if (QString::fromLatin1(name) == "diagnosticsGroup") {
                        const bool before = settings->values().value("debug").toBool();
                        activate(find("diagnosticsDebugSwitch"));
                        QCOMPARE(settings->values().value("debug").toBool(), !before);

                        settings->setValue("debug", before);
                    }
                }
                QVERIFY(QMetaObject::invokeMethod(frame, "reveal", Q_ARG(QVariant, QVariant::fromValue(font))));
            }
        }
        auto* font = find("fontFamilyCombo");
        auto* input = qobject_cast<QQuickItem*>(font->property("contentItem").value<QObject*>());
        QVERIFY(input);
        input->forceActiveFocus();
        const auto previousFamily = settings->values().value("fontFamily").toString();
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        for (const auto ch : QString("Segoe UI")) QTest::keyClick(window, ch.toLatin1());
        QCOMPARE(settings->values().value("fontFamily").toString(), previousFamily);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_COMPARE(settings->values().value("fontFamily").toString(), QString("Segoe UI"));
        QTest::keyClick(window, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClick(window, Qt::Key_Backspace);
        QTest::keyClick(window, Qt::Key_Tab);
        QTRY_COMPARE(font->property("editText").toString(), QString("Segoe UI"));
        QCOMPARE(warnings.count(), 0);
        backend.stop();
    }
};
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);

    QFont font = app.font();
    font.setFamilies({"Segoe UI", "Microsoft YaHei UI"});
    if (qEnvironmentVariableIsSet("DANMAKU_UI_LARGE_TEXT"))
        font.setPointSizeF(13.5);
    app.setFont(font);
    if (QFontDatabase::families().contains("Microsoft YaHei UI"))
        QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Han, "Microsoft YaHei UI");
    qmlRegisterType<DanmakuItem>("DanmaX.Native", 1, 0, "DanmakuCanvas");
    QWK::registerTypes(nullptr);
    UiWorkflowTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "UiWorkflowTests.moc"

