#include <QGuiApplication>
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

class UiWorkflowTests : public QObject {
    Q_OBJECT
private slots:
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
        auto* combo = window->findChild<QQuickItem*>("probeChoice");
        QVERIFY(combo);
        auto* popup = combo->property("popup").value<QObject*>();
        auto* list = qobject_cast<QQuickItem*>(combo->property("popupList").value<QObject*>());
        QVERIFY(popup && list);
        auto* bar = list->findChild<QQuickItem*>("comboPopupScrollBar");
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
        combo->forceActiveFocus();
        QTest::keyClick(window, Qt::Key_Space);
        QTRY_VERIFY(popup->property("opened").toBool());
        bounds();
        QVERIFY(bar->isVisible());
        QVERIFY(list->property("contentY").toReal() > 0);
        auto* current = qobject_cast<QQuickItem*>(list->property("currentItem").value<QObject*>());
        QVERIFY(current);
        QVERIFY(current->width() + bar->width() + 7 <= list->width());
        QVERIFY(current->y() + current->height() <= list->property("contentY").toReal() + list->height() + 1);
        QVERIFY(window->grabWindow().save(output + "/long-list.png"));
        auto* surface = popup->property("background").value<QObject*>();
        QVERIFY(surface);
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
        QTest::qWait(80);
        const QColor lightSurface = surface->property("color").value<QColor>();
        QCOMPARE(lightSurface.alpha(), 255);
        QVERIFY(window->grabWindow().save(output + "/long-list-light.png"));
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
        QTest::qWait(80);
        const QColor darkSurface = surface->property("color").value<QColor>();
        QVERIFY(darkSurface != lightSurface);
        QCOMPARE(darkSurface.alpha(), 255);
        QVERIFY(window->grabWindow().save(output + "/long-list-dark.png"));
        QTest::keyClick(window, Qt::Key_Up);
        QTest::keyClick(window, Qt::Key_Return);
        QTRY_VERIFY(!popup->property("visible").toBool());
        QCOMPARE(combo->property("currentIndex").toInt(), 58);
        QVERIFY(combo->hasActiveFocus());
        for (const int width : {1080, 520}) {
            window->resize(width, 480);
            window->setProperty("anchorY", 400);
            QMetaObject::invokeMethod(popup, "open");
            QTRY_VERIFY(popup->property("opened").toBool());
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
        QMetaObject::invokeMethod(popup, "open");
        QTRY_VERIFY(popup->property("opened").toBool());
        bounds();
        QVERIFY(!bar->isVisible());
        combo->setProperty("model", rows);
        QTRY_VERIFY(bar->isVisible());
        bounds();
        // Drag the real standard scrollbar, then wheel the original popup ListView.
        const QPoint from = bar->mapToScene(QPointF(bar->width() / 2, 5)).toPoint();
        const QPoint to = bar->mapToScene(QPointF(bar->width() / 2, bar->height() - 3)).toPoint();
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
        QVERIFY(!bar->isVisible());
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
        auto find = [&](const char* name) { return window->findChild<QQuickItem*>(QString::fromLatin1(name)); };
        auto activate = [&](QQuickItem* item) {
            QVERIFY(item);
            item->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Space);
            QTest::qWait(50);
        };
        auto* start = find("startPlayback");
        QVERIFY(start);
        QVERIFY(!start->isEnabled());
        QVERIFY(!find("stopPlayback")->isVisible());
        auto* disclosure = find("sessionDisclosure");
        QVERIFY(disclosure);
        disclosure->setProperty("expanded", true);
        find("sessionIdInput")->forceActiveFocus();
        disclosure->setProperty("expanded", false);
        QVERIFY(disclosure->findChild<QQuickItem*>("disclosureToggle")->hasActiveFocus());
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
        QTRY_VERIFY(find("danmakuServerInput")->hasActiveFocus());
        const auto serverPosition = find("danmakuServerInput")->mapToScene(QPointF());
        QVERIFY(serverPosition.y() >= 0 && serverPosition.y() < window->height());
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(!find("danmakuServerInput")->hasActiveFocus());
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
                const QPoint dragStart = bar->mapToScene(QPointF(bar->width() / 2, 5)).toPoint();
                const QPoint dragEnd = bar->mapToScene(QPointF(bar->width() / 2, bar->height() - 2)).toPoint();
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
    qmlRegisterType<DanmakuItem>("LocalDanmaku.Native", 1, 0, "DanmakuCanvas");
    UiWorkflowTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "UiWorkflowTests.moc"
