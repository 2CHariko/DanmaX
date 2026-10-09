#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <cstdio>
#include <QWheelEvent>

// Minimal C++ replacement for the library's documented Python theme bridge.
// No control source or drawing code is modified.
class ThemeBridge : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    QString mode = "Light";
    Q_INVOKABLE QString get_theme_name() const { return mode; }
    Q_INVOKABLE QString get_theme() const { return mode; }
    Q_INVOKABLE QString get_theme_color() const { return "#0067c0"; }
    Q_INVOKABLE void toggle_theme(QString value) { mode = value; emit themeChanged(mode); }
signals:
    void themeChanged(QString theme);
    void backdropChanged(QString effect);
};

int main(int argc, char **argv) {
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &message) {
        const auto bytes = message.toUtf8();
        std::fprintf(stderr, "%s\n", bytes.constData());
        std::fflush(stderr);
    });
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 5) return 2;
    ThemeBridge theme;
    theme.mode = args[3];
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("ThemeManager", &theme);
    engine.rootContext()->setContextProperty("probeDark", theme.mode == "Dark");
    engine.addImportPath(args[2]);
    engine.load(QUrl::fromLocalFile(args[1]));
    if (engine.rootObjects().isEmpty()) return 3;
    auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (!window) return 4;
    const QString output = args[4];
    QDir().mkpath(output);
    if (args.contains("--interactive")) return app.exec();
    QTimer::singleShot(1000, &app, [&] {
        QJsonObject report;
        auto *combo = window->findChild<QQuickItem *>("longCombo");
        auto *page = window->findChild<QQuickItem *>("pageScroll");
        auto *expander = window->findChild<QQuickItem *>("expander");
        window->grabWindow().save(output + "/page.png");
        if (combo) {
            combo->forceActiveFocus();
            QTest::keyClick(window, Qt::Key_Space);
            QTest::qWait(1200);
            auto *popup = combo->property("popup").value<QObject *>();
            report["spaceOpensPopup"] = popup && popup->property("visible").toBool();
            if (popup && !popup->property("visible").toBool()) QMetaObject::invokeMethod(popup, "open");
            QTest::qWait(1200);
            window->grabWindow().save(output + "/popup.png");
            int before = combo->property("currentIndex").toInt();
            QTest::keyClick(window, Qt::Key_Up);
            QTest::keyClick(window, Qt::Key_Return);
            QTest::qWait(1200);
            report["keyboardBefore"] = before;
            report["keyboardAfter"] = combo->property("currentIndex").toInt();
            report["popupClosedAfterEnter"] = popup && !popup->property("visible").toBool();
            if (popup) QMetaObject::invokeMethod(popup, "close");
            QTest::qWait(300);
            if (popup) {
                combo->setProperty("currentIndex", 39);
                QMetaObject::invokeMethod(popup, "open");
                QTest::qWait(1200);
                if (combo->metaObject()->indexOfProperty("popupLimit") >= 0) {
                    report["popupLimitRespected"] = popup->property("height").toReal() <= combo->property("popupLimit").toReal();
                    report["popupOpensUpNearBottom"] = popup->property("y").toReal() < 0;
                    auto *list = popup->property("contentItem").value<QQuickItem *>();
                    auto *bar = list ? list->findChild<QQuickItem *>("popupScrollBar") : nullptr;
                    if (list && bar) {
                        const auto from = bar->mapToScene(QPointF(bar->width()/2, bar->height()*0.82)).toPoint();
                        const auto to = bar->mapToScene(QPointF(bar->width()/2, bar->height()*0.18)).toPoint();
                        report["popupScrollBeforeDrag"] = list->property("contentY").toReal();
                        QTest::mouseMove(window, from);
                        QTest::qWait(500);
                        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
                        QTest::mouseMove(window, to, 150);
                        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
                        QTest::qWait(300);
                        report["popupScrollAfterDrag"] = list->property("contentY").toReal();
                        const auto p = list->mapToScene(QPointF(list->width()/2, list->height()/2));
                        QWheelEvent wheel(p, window->mapToGlobal(p.toPoint()), QPoint(), QPoint(0,-120), Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
                        QCoreApplication::sendEvent(window, &wheel);
                        QTest::qWait(500);
                        report["popupScrollAfterWheel"] = list->property("contentY").toReal();
                        window->grabWindow().save(output + "/popup-scroll.png");
                    }
                    QMetaObject::invokeMethod(popup, "close");
                    page->setProperty("contentY", 240);
                    QTest::qWait(200);
                    QMetaObject::invokeMethod(popup, "open");
                    QTest::qWait(500);
                    report["popupOpensDownNearTop"] = popup->property("y").toReal() >= combo->height();
                    window->grabWindow().save(output + "/popup-down.png");
                    QMetaObject::invokeMethod(popup, "close");
                    page->setProperty("contentY", 0);
                    QTest::qWait(200);
                    QMetaObject::invokeMethod(popup, "open");
                    QTest::qWait(500);
                }
                window->resize(520, 500);
                QTest::qWait(1200);
                window->grabWindow().save(output + "/popup-resized.png");
                auto *content = popup->property("contentItem").value<QQuickItem *>();
                if (content) {
                    const auto rect = content->mapRectToScene(content->boundingRect());
                    report["popupContentInsideResizedWindow"] = QRectF(0, 0, window->width(), window->height()).contains(rect);
                    report["resizedPopupWidth"] = rect.width();
                    report["resizedPopupHeight"] = rect.height();
                }
                QMetaObject::invokeMethod(popup, "close");
                window->resize(900, 700);
                QTest::qWait(500);
            }
        }
        if (expander) {
            const char *prop = expander->metaObject()->indexOfProperty("expanded") >= 0 ? "expanded" : "expand";
            QJsonArray tabStops;
            bool reached = false;
            for (int i = 0; i < 35; ++i) {
                QTest::keyClick(window, Qt::Key_Tab);
                QTest::qWait(20);
                auto *focus = window->activeFocusItem();
                if (!focus) continue;
                tabStops.append(QString::fromLatin1(focus->metaObject()->className()));
                if (focus == expander || expander->isAncestorOf(focus)) {
                    reached = true;
                    const bool before = expander->property(prop).toBool();
                    QTest::keyClick(window, Qt::Key_Space);
                    QTest::qWait(1200);
                    report["expanderSpaceToggles"] = before != expander->property(prop).toBool();
                    break;
                }
            }
            report["expanderReachableByTab"] = reached;
            report["tabFocusClasses"] = tabStops;
            expander->setProperty(prop, true);
            QTest::qWait(1200);
            report["expanderHeight"] = expander->height();
            window->grabWindow().save(output + "/expanded.png");
            if (auto *inner = window->findChild<QQuickItem *>("innerCheck")) {
                inner->forceActiveFocus();
                expander->setProperty(prop, false);
                QTest::qWait(300);
                report["collapsedContentRetainsFocus"] = inner->hasActiveFocus();
                report["collapseFocusReturnedToHeader"] = expander->hasActiveFocus();
                expander->setProperty(prop, true);
            }
        }
        if (page) {
            page->setProperty("contentY", 0);
            QTest::qWait(200);
            auto *bar = window->findChild<QQuickItem *>("pageScrollBar");
            if (bar) {
                auto from = bar->mapToScene(QPointF(bar->width()/2, bar->height()*0.2)).toPoint();
                auto to = bar->mapToScene(QPointF(bar->width()/2, bar->height()*0.9)).toPoint();
                QTest::mouseMove(window, from);
                QTest::qWait(500);
                QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, from);
                QTest::mouseMove(window, to, 150);
                QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, to);
                QTest::qWait(300);
                report["contentYAfterScrollbarDrag"] = page->property("contentY").toReal();
            }
            const auto bottom = page->property("contentHeight").toReal() - page->height();
            page->setProperty("contentY", bottom);
            QTest::qWait(200);
            window->grabWindow().save(output + "/bottom.png");
            report["bottomContentY"] = page->property("contentY").toReal();
        }
        window->resize(520, 600);
        QTest::qWait(1200);
        window->grabWindow().save(output + "/narrow.png");
        if (window->metaObject()->indexOfMethod("toggleTheme()") >= 0) {
            const auto before = window->property("currentDark").toBool();
            QMetaObject::invokeMethod(window, "toggleTheme");
            QTest::qWait(300);
            report["runtimeThemeChanged"] = before != window->property("currentDark").toBool();
            page->setProperty("contentY", 0);
            QMetaObject::invokeMethod(window, "scaleText");
            QTest::qWait(300);
            window->grabWindow().save(output + "/large-text-theme.png");
            report["scaledComboFontPixels"] = combo->property("font").value<QFont>().pixelSize();
        }
        report["qtVersion"] = qVersion();
        report["dpr"] = window->devicePixelRatio();
        bool passed = true;
        if (combo->metaObject()->indexOfProperty("popupLimit") >= 0) {
            for (const auto *key : {"spaceOpensPopup", "popupClosedAfterEnter", "popupContentInsideResizedWindow", "popupLimitRespected", "popupOpensDownNearTop", "popupOpensUpNearBottom", "expanderReachableByTab", "expanderSpaceToggles", "runtimeThemeChanged", "collapseFocusReturnedToHeader"})
                passed = passed && report[key].toBool();
            passed = passed && report["keyboardAfter"].toInt() == 38
                && !report["collapsedContentRetainsFocus"].toBool()
                && report["popupScrollAfterDrag"].toDouble() < report["popupScrollBeforeDrag"].toDouble()
                && report["popupScrollAfterWheel"].toDouble() > report["popupScrollAfterDrag"].toDouble()
                && report["scaledComboFontPixels"].toInt() == 21;
            report["checksPassed"] = passed;
        }
        QFile file(output + "/results.json");
        file.open(QIODevice::WriteOnly);
        file.write(QJsonDocument(report).toJson());
        app.exit(passed ? 0 : 5);
    });
    return app.exec();
}
#include "main.moc"

