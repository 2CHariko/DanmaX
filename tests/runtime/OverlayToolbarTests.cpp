#include "application/AppController.h"
#include "platform/windows/ToolbarInputFilter.h"
#include "infrastructure/SettingsStore.h"
#include "renderer/DanmakuItem.h"
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QWindow>
#include <QTest>

class OverlayToolbarTests : public QObject {
    Q_OBJECT
private slots:
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
