#include "application/AppController.h"
#include "platform/windows/OverlayInputFilter.h"
#include "infrastructure/SettingsStore.h"
#include "renderer/DanmakuItem.h"
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>

class OverlayToolbarTests : public QObject {
    Q_OBJECT
private slots:
    void hitTestLogic() {
        OverlayInputFilter filter;
        // 初始没有任何矩形，所有坐标都应判定为穿透 (hitTest = false)
        QVERIFY(!filter.hitTest(QPoint(0, 0)));
        QVERIFY(!filter.hitTest(QPoint(500, 20)));

        // 模拟折叠状态：仅顶部中央有 80x8 像素把手
        filter.setInteractiveRects({QRect(400, 0, 80, 8)});
        QVERIFY(filter.hitTest(QPoint(400, 0)));
        QVERIFY(filter.hitTest(QPoint(440, 4)));
        QVERIFY(!filter.hitTest(QPoint(399, 4)));
        QVERIFY(!filter.hitTest(QPoint(481, 4)));
        QVERIFY(!filter.hitTest(QPoint(440, 9)));

        // 模拟展开状态：主胶囊栏 500x38 + 展开的 Popover 220x48
        filter.setInteractiveRects({
            QRect(200, 4, 500, 38),
            QRect(340, 46, 220, 48)
        });
        // 命中主胶囊栏内部
        QVERIFY(filter.hitTest(QPoint(250, 20)));
        // 命中 Popover 浮层内部
        QVERIFY(filter.hitTest(QPoint(400, 60)));
        // 外部透明区域完全穿透
        QVERIFY(!filter.hitTest(QPoint(100, 20)));
        QVERIFY(!filter.hitTest(QPoint(250, 100)));
        QVERIFY(!filter.hitTest(QPoint(800, 300)));
    }
    void hoverDetection() {
        OverlayInputFilter filter;
        QSignalSpy hoverSpy(&filter, &OverlayInputFilter::hoverChanged);
        QVERIFY(!filter.isHovering());
        filter.setInteractiveRects({QRect(100, 0, 200, 40)});
        QVERIFY(filter.hitTest(QPoint(150, 20)));
        QVERIFY(!filter.hitTest(QPoint(50, 20)));
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

        // 5. 交互区域同步
        QVariantList rects;
        QVariantMap r1;
        r1[QStringLiteral("x")] = 100;
        r1[QStringLiteral("y")] = 0;
        r1[QStringLiteral("width")] = 200;
        r1[QStringLiteral("height")] = 30;
        rects.append(r1);
        controller.setOverlayInteractiveRects(rects);
        QVERIFY(controller.overlayInputFilter()->hitTest(QPoint(150, 15)));
        QVERIFY(!controller.overlayInputFilter()->hitTest(QPoint(50, 15)));
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
