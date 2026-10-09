
#include <QGuiApplication>
#include <QCursor>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTest>
#include <QDir>
#include <QSignalSpy>
#include <QFontDatabase>
#include <QTemporaryDir>
#include <QFile>
#include <QWheelEvent>
#include <QStyleHints>
#include "application/AppController.h"
#include "renderer/DanmakuItem.h"
#include "infrastructure/SettingsStore.h"
#include "infrastructure/LogModel.h"
#include "platform/windows/WindowBackdropController.h"
#include <QWKQuick/qwkquickglobal.h>

class UiWorkflowTests : public QObject {
    Q_OBJECT
private slots:
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
        QCOMPARE(settings->values()["danmakuServers"].toStringList().size(), 2);
        auto* onlineFrame = find("danmakuServerInput")->parentItem();
        while (onlineFrame && !onlineFrame->property("bodyItem").isValid()) onlineFrame = onlineFrame->parentItem();
        QVERIFY(onlineFrame);
        for (const auto& theme : {QString("light"), QString("dark")}) {
            settings->setValue("theme", theme);
            window->resize(520, 640);
            QVERIFY(QMetaObject::invokeMethod(onlineFrame, "reveal", Q_ARG(QVariant, QVariant::fromValue(find("danmakuServerInput")))));
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(output + "/online-servers-" + theme + ".png"));
        }
        activate(find("serverUp1"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList().first(), QString("https://backup.test"));
        activate(find("serverDown0"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList().first(), defaults.first());
        activate(find("serverRemove1"));
        QCOMPARE(settings->values()["danmakuServers"].toStringList(), defaults);
        activate(find("serverRemove0"));
        QVERIFY(settings->values()["danmakuServers"].toStringList().isEmpty());
        QVERIFY(find("addDanmakuServer")->hasActiveFocus());
        QVERIFY(QMetaObject::invokeMethod(onlineFrame, "focusOnlineSettings"));
        QVERIFY(find("addDanmakuServer")->hasActiveFocus());
        QVERIFY(settings->setDanmakuServers(defaults));
        QTRY_VERIFY(find("danmakuServerInput"));
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
