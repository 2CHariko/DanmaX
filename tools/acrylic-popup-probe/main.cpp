#include "BackdropBridge.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QScreen>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTest>
#include <QWheelEvent>
#include <QQuickView>
#include <cmath>
#include <array>

class ProbeTests final : public QObject {
    Q_OBJECT
public:
    ProbeTests(BackdropBridge& bridge, QQmlApplicationEngine& engine) : bridge_(bridge), engine_(engine) {}
private slots:
    void nativeWindowAndSelectedAlignment() {
        window_ = qobject_cast<QQuickWindow*>(engine_.rootObjects().value(0)); QVERIFY(window_);
        const auto area = window_->screen()->availableGeometry();
        window_->setPosition(area.topLeft() + QPoint(64, 48));
        window_->raise(); window_->requestActivate();
        report_["nativeMainActivation"] = bool(SetForegroundWindow(reinterpret_cast<HWND>(window_->winId())));
        QVERIFY(QTest::qWaitForWindowExposed(window_));
        QVERIFY(QTest::qWaitForWindowActive(window_));
        // One real click confined to this test window. Synthetic Qt events can
        // report Qt focus without changing the OS foreground/Acrylic policy.
        const auto host = reinterpret_cast<HWND>(window_->winId());
        if (GetForegroundWindow() != host) {
            QTest::qWait(150);
            POINT old{}, point{24, 180}; GetCursorPos(&old); ClientToScreen(host, &point);
            if (GetAncestor(WindowFromPoint(point), GA_ROOT) == host && SetCursorPos(point.x, point.y)) {
                INPUT click[2]{}; click[0].type = click[1].type = INPUT_MOUSE;
                click[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN; click[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
                const auto sent = SendInput(2, click, sizeof(INPUT));
                report_["activationClickSent"] = sent == 2;
                if (sent != 2) SendInput(1, &click[1], sizeof(INPUT));
                QTest::qWait(150); SetCursorPos(old.x, old.y);
            }
        }
        report_["nativeHostForeground"] = GetForegroundWindow() == host;
        report_["screenName"] = window_->screen()->name();
        const auto screenRect = window_->screen()->geometry();
        report_["screenX"] = screenRect.x(); report_["screenY"] = screenRect.y();
        report_["screenWidth"] = screenRect.width(); report_["screenHeight"] = screenRect.height();
        report_["screenRefreshRate"] = window_->screen()->refreshRate();
        short_ = window_->findChild<QQuickItem*>("shortChoice"); long_ = window_->findChild<QQuickItem*>("longChoice");
        QVERIFY(short_ && long_);
        const auto output = evidence(); QVERIFY(QDir().mkpath(output));
        for (const bool dark : {false, true}) {
            QVERIFY(QMetaObject::invokeMethod(window_, "theme", Q_ARG(QVariant, dark)));
            QVERIFY(QMetaObject::invokeMethod(window_, "openChoice", Q_ARG(QVariant, 0)));
            auto* popup = short_->property("popup").value<QObject*>(); QVERIFY(popup);
            QTRY_VERIFY(popup->property("opened").toBool());
            auto* list = popup->property("contentItem").value<QQuickItem*>(); QVERIFY(list);
            QTRY_VERIFY(list->window() && list->window() != window_);
            QVERIFY((list->window()->flags() & Qt::WindowType_Mask) == Qt::Popup);
            QTRY_VERIFY_WITH_TIMEOUT(bridge_.effective(list->window()), 3000);
            QVERIFY(short_->property("faceplateHidden").toBool());
            QVERIFY2(checkAlignment(short_), "Selected row is not aligned with the original field; see maxAlignmentError in result.json.");
            QTest::qWait(250);
            const auto name = dark ? "dark" : "light";
            report_["mainVisible"] = window_->isVisible(); report_["mainActive"] = window_->isActive();
            report_["mainHwnd"] = QString::number(window_->winId(), 16);
            report_["foregroundHwnd"] = QString::number(reinterpret_cast<quintptr>(GetForegroundWindow()), 16);
            QVERIFY(window_->grabWindow().save(output + "/" + name + "-qt-local.png"));
            QVERIFY(capture(window_).save(output + "/" + name + "-desktop.png"));
            QVERIFY(capture(list->window()).save(output + "/" + name + "-popup.png"));
            report_[dark ? "darkDesktopCaptureValid" : "lightDesktopCaptureValid"] = desktopCaptureValid();
            QTest::keyClick(list->window(), Qt::Key_Escape);
            QTRY_VERIFY(!popup->property("visible").toBool());
            QTRY_VERIFY(short_->hasActiveFocus());
        }
        report_["nativePopup"] = true; report_["selectedAlignment"] = true; report_["escapeFocus"] = true;
    }
    void visualAcrylicAndFallback_data() {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }
    void visualAcrylicAndFallback() {
        QFETCH(bool, dark);
        QVERIFY(window_ && short_);
        QVERIFY(QMetaObject::invokeMethod(window_, "theme", Q_ARG(QVariant, dark)));
        window_->setProperty("warmPattern", false);
        const QString prefix = dark ? "/dark-" : "/light-";
        short_->setProperty("currentIndex", 2);
        QVERIFY(QMetaObject::invokeMethod(window_, "openChoice", Q_ARG(QVariant, 0)));
        auto* popup = short_->property("popup").value<QObject*>(); QTRY_VERIFY(popup->property("opened").toBool());
        auto* list = popup->property("contentItem").value<QQuickItem*>(); QVERIFY(list);
        auto* native = list->window(); QVERIFY(native);
        QTRY_VERIFY(bridge_.effective(native)); QTest::qWait(300);
        QVERIFY(popup->property("opened").toBool() && native->isVisible());
        const auto cold = capture(native); QVERIFY(!cold.isNull()); QVERIFY(cold.save(evidence() + prefix + "acrylic-cold.png"));
        window_->setProperty("warmPattern", true); QTest::qWait(350);
        const auto warm = capture(native); QVERIFY(warm.save(evidence() + prefix + "acrylic-warm.png"));
        QVERIFY(popup->property("opened").toBool() && native->isVisible());
        const auto foreground = GetForegroundWindow();
        const bool active = foreground == reinterpret_cast<HWND>(window_->winId())
            || foreground == reinterpret_cast<HWND>(native->winId());
        report_[dark ? "darkVisualForegroundValid" : "lightVisualForegroundValid"] = active;
        const bool valid = desktopCaptureValid() && active;
        report_[dark ? "darkVisualEvidenceValid" : "lightVisualEvidenceValid"] = valid;
        if (!valid) {
            QTest::keyClick(native, Qt::Key_Escape);
            QFAIL("Desktop pattern or OS foreground validation failed; Acrylic result is inconclusive.");
        }
        const auto a = gutter(cold), b = gutter(warm);
        const double response = std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]);
        report_["backdropColorResponse"] = response; report_["horizontalEdgeEnergy"] = b[3];
        report_[dark ? "darkBackdropColorResponse" : "lightBackdropColorResponse"] = response;
        report_[dark ? "darkEdgeEnergy" : "lightEdgeEnergy"] = b[3];
        report_["nativeState"] = bridge_.report();
        const auto host = reinterpret_cast<HWND>(native->winId());
        BOOL nc{}; DwmGetWindowAttribute(host, DWMWA_NCRENDERING_ENABLED, &nc, sizeof(nc));
        report_["nonClientRendering"] = bool(nc);
        report_["nativeStyle"] = QString::number(GetWindowLongPtrW(host, GWL_STYLE), 16);
        report_["nativeExtendedStyle"] = QString::number(GetWindowLongPtrW(host, GWL_EXSTYLE), 16);
        bridge_.setRequested(false); QTRY_VERIFY(!bridge_.effective(native)); QTest::qWait(150);
        const auto solidWarm = capture(native); QVERIFY(solidWarm.save(evidence() + prefix + "fallback-warm.png"));
        window_->setProperty("warmPattern", false); QTest::qWait(150);
        const auto solidCold = capture(native); QVERIFY(solidCold.save(evidence() + prefix + "fallback-cold.png"));
        QVERIFY(popup->property("opened").toBool() && native->isVisible());
        const auto c = gutter(solidWarm), d = gutter(solidCold);
        const double fallbackResponse = std::abs(c[0] - d[0]) + std::abs(c[1] - d[1]) + std::abs(c[2] - d[2]);
        report_["fallbackColorResponse"] = fallbackResponse;
        report_["opaqueFallback"] = fallbackResponse < 2;
        bridge_.setRequested(true); QTRY_VERIFY(bridge_.effective(native));
        QTest::keyClick(native, Qt::Key_Escape); QTRY_VERIFY(!popup->property("visible").toBool());
        report_["visualAcrylic"] = response > 3 && b[3] < 8;
        QVERIFY2(response > 3, "DWM accepted the material, but desktop capture did not respond to content behind it.");
        QVERIFY2(b[3] < 8, "Backdrop stripes remain sharp: transparency is not sufficient evidence of Acrylic blur.");
        QVERIFY(fallbackResponse < 2); report_["opaqueFallback"] = true;
    }
private:
    void checkIndependentTopLevel(bool dark) {
        QVERIFY(window_);
        QVERIFY(QMetaObject::invokeMethod(window_, "closeChoices"));
        QVERIFY(QMetaObject::invokeMethod(window_, "theme", Q_ARG(QVariant, dark)));
        window_->setProperty("warmPattern", false);
        QQuickView panel;
        panel.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        panel.setTransientParent(window_);
        panel.setColor(Qt::transparent);
        panel.setResizeMode(QQuickView::SizeRootObjectToView);
        panel.rootContext()->setContextProperty("bridge", &bridge_);
        panel.setSource(QUrl("qrc:/probe/ProbeSurface.qml"));
        QVERIFY(panel.status() == QQuickView::Ready);
        panel.resize(280, 160);
        bridge_.watchTopLevel(&panel);
        panel.setPosition(window_->mapToGlobal(QPoint(120, 264)));
        panel.show(); panel.raise(); panel.requestActivate();
        QVERIFY(QTest::qWaitForWindowExposed(&panel));
        QTRY_VERIFY(bridge_.effective(&panel));
        QTest::qWait(350);
        const auto cold = capture(&panel);
        window_->setProperty("warmPattern", true); QTest::qWait(350);
        const auto warm = capture(&panel);
        QVERIFY(panel.isVisible());
        const QString prefix = dark ? "/dark-top-level-" : "/light-top-level-";
        QVERIFY(cold.save(evidence() + prefix + "cold.png"));
        QVERIFY(warm.save(evidence() + prefix + "warm.png"));
        const auto foreground = GetForegroundWindow();
        const bool active = foreground == reinterpret_cast<HWND>(window_->winId())
            || foreground == reinterpret_cast<HWND>(panel.winId());
        report_[dark ? "darkTopLevelForegroundValid" : "lightTopLevelForegroundValid"] = active;
        const bool valid = desktopCaptureValid() && active;
        report_[dark ? "darkTopLevelEvidenceValid" : "lightTopLevelEvidenceValid"] = valid;
        QVERIFY2(valid, "Desktop pattern or OS foreground validation failed; top-level Acrylic is inconclusive.");
        const auto a = gutter(cold), b = gutter(warm);
        const double response = std::abs(a[0] - b[0]) + std::abs(a[1] - b[1]) + std::abs(a[2] - b[2]);
        report_[dark ? "darkTopLevelResponse" : "lightTopLevelResponse"] = response;
        report_[dark ? "darkTopLevelEdgeEnergy" : "lightTopLevelEdgeEnergy"] = b[3];
        report_["topLevelState"] = bridge_.report();
        panel.close();
        QVERIFY2(response > 3, "Independent frameless window still has no backdrop color response.");
        QVERIFY2(b[3] < 8, "Independent frameless window is transparent without sufficient blur.");
    }
private slots:
    void selectionAndLongList() {
        QVERIFY(window_ && short_ && long_);
        auto* popup = short_->property("popup").value<QObject*>();
        QVERIFY(QMetaObject::invokeMethod(window_, "openChoice", Q_ARG(QVariant, 0)));
        QTRY_VERIFY(popup->property("opened").toBool());
        auto* native = popup->property("contentItem").value<QQuickItem*>()->window();
        QTest::keyClick(native, Qt::Key_Down); QTest::keyClick(native, Qt::Key_Return);
        QTRY_COMPARE(short_->property("currentIndex").toInt(), 3); QTRY_VERIFY(!popup->property("visible").toBool());
        for (const int index : {0, 59, 119}) {
            long_->setProperty("currentIndex", index);
            QVERIFY(QMetaObject::invokeMethod(window_, "openChoice", Q_ARG(QVariant, 1)));
            auto* longPopup = long_->property("popup").value<QObject*>();
            QTRY_VERIFY(longPopup->property("opened").toBool());
            QVERIFY2(checkAlignment(long_), "Selected row is not aligned; long-list input checks cannot proceed.");
            auto* list = longPopup->property("contentItem").value<QQuickItem*>(); QVERIFY(list);
            native = list->window(); QTRY_VERIFY(bridge_.effective(native));
            QVERIFY(capture(window_).save(evidence() + QString("/long-%1-desktop.png").arg(index)));
            const auto before = list->property("contentY").toDouble();
            const auto point = list->mapToScene(QPointF(list->width() / 2, list->height() / 2));
            QTest::mouseMove(native, point.toPoint());
            QTest::wheelEvent(native, point, QPoint(0, index == 119 ? 120 : -120));
            QTRY_VERIFY(list->property("contentY").toDouble() != before);
            auto* outside = window_->findChild<QQuickItem*>("outsideInput"); QVERIFY(outside);
            const auto click = outside->mapToScene(QPointF(20, outside->height() / 2)).toPoint();
            QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, click);
            QTRY_VERIFY(!longPopup->property("visible").toBool());
        }
        report_["selectionKeyboard"] = true; report_["longListAndWheel"] = true; report_["outsideClose"] = true;
    }
    void menuAndLifecycle() {
        QVERIFY(window_ && short_ && long_);
        QVERIFY(QMetaObject::invokeMethod(window_, "closeChoices"));
        QVERIFY(QMetaObject::invokeMethod(window_, "openMenu"));
        auto* menu = window_->findChild<QObject*>("probeMenu"); QVERIFY(menu); QTRY_VERIFY(menu->property("opened").toBool());
        auto* menuSurface = menu->property("background").value<QQuickItem*>(); QVERIFY(menuSurface);
        auto* native = menuSurface->window(); QVERIFY(native != window_); QTRY_VERIFY(bridge_.effective(native));
        QTest::qWait(150); QVERIFY(capture(window_).save(evidence() + "/menu-desktop.png"));
        QTest::keyClick(native, Qt::Key_Escape); QTRY_VERIFY(!menu->property("visible").toBool());
        for (int i = 0; i < 12; ++i) {
            QVERIFY(QMetaObject::invokeMethod(window_, "openChoice", Q_ARG(QVariant, i % 2)));
            auto* current = (i % 2 ? long_ : short_)->property("popup").value<QObject*>();
            QTRY_VERIFY(current->property("opened").toBool());
            QVERIFY(QMetaObject::invokeMethod(current, "close")); QTRY_VERIFY(!current->property("visible").toBool());
        }
        QVERIFY(bridge_.windows() <= 3);
        report_["menu"] = true; report_["reopenCycles"] = 12;
    }
    void independentTopLevel_data() { visualAcrylicAndFallback_data(); }
    void independentTopLevel() { QFETCH(bool, dark); checkIndependentTopLevel(dark); }
    void cleanupTestCase() {
        report_["qtVersion"] = qVersion(); report_["osBuild"] = QOperatingSystemVersion::current().microVersion();
        report_["nativeStyleContrast"] = bridge_.nativeStyleContrast();
        report_["mode"] = qEnvironmentVariable("DANMAKU_ACRYLIC_MODE");
        report_["noRedirectionRequested"] = qEnvironmentVariable("QT_QPA_DISABLE_REDIRECTION_SURFACE") == "1";
        report_["windowKitRequested"] = bridge_.useWindowKit();
        report_["dpr"] = window_ ? window_->devicePixelRatio() : 0; report_["remainingWindows"] = bridge_.windows();
        QFile output(evidence() + "/result.json"); QVERIFY(output.open(QIODevice::WriteOnly));
        output.write(QJsonDocument(report_).toJson()); output.close();
        engine_.clearComponentCache(); engine_.rootObjects().first()->deleteLater();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        report_["afterDestroy"] = bridge_.windows(); QVERIFY(bridge_.windows() == 0);
        QVERIFY(output.open(QIODevice::WriteOnly | QIODevice::Truncate)); output.write(QJsonDocument(report_).toJson());
    }
private:
    QString evidence() const { return qEnvironmentVariable("DANMAKU_ACRYLIC_EVIDENCE"); }
    QImage capture(QQuickWindow* target) const {
        // With WId=0 Qt adds this screen's origin internally. Global coordinates
        // double-count the offset on a secondary monitor and can capture all black.
        const auto local = target->mapToGlobal(QPoint()) - target->screen()->geometry().topLeft();
        return target->screen()->grabWindow(0, local.x(), local.y(), target->width(), target->height()).toImage();
    }
    bool desktopCaptureValid() const {
        const auto image = capture(window_);
        if (image.isNull()) return false;
        const double scale = image.devicePixelRatio();
        int low = 255, high = 0;
        // An uncovered strip of the known host pattern, outside every probe panel.
        for (int x = qRound(792 * scale); x < qRound(824 * scale) && x < image.width(); ++x) {
            const int value = image.pixelColor(x, qRound(240 * scale)).red();
            low = std::min(low, value); high = std::max(high, value);
        }
        return high - low > 30;
    }
    std::array<double, 4> gutter(const QImage& image) const {
        std::array<double, 4> result{}; const double scale = image.devicePixelRatio();
        const int first = image.width() - qRound(22 * scale), last = image.width() - qRound(8 * scale);
        int count = 0, edges = 0;
        for (int y = qRound(8 * scale); y < image.height() - qRound(8 * scale); ++y)
            for (int x = first; x < last; ++x) {
                const auto color = image.pixelColor(x, y); result[0] += color.red(); result[1] += color.green(); result[2] += color.blue(); ++count;
                if (x > first) { const auto previous = image.pixelColor(x - 1, y);
                    result[3] += (std::abs(color.red() - previous.red()) + std::abs(color.green() - previous.green()) + std::abs(color.blue() - previous.blue())) / 3.0; ++edges; }
            }
        for (int i = 0; i < 3; ++i) result[i] /= std::max(1, count);
        result[3] /= std::max(1, edges); return result;
    }
    bool checkAlignment(QQuickItem* choice) {
        auto* popup = choice->property("popup").value<QObject*>();
        auto* list = popup->property("contentItem").value<QQuickItem*>();
        if (!list) return false;
        const double row = choice->property("rowHeight").toDouble();
        const double y = choice->property("currentIndex").toInt() * row - list->property("contentY").toDouble() + row / 2;
        const auto selected = list->mapToGlobal(QPointF(0, y));
        const auto field = choice->mapToGlobal(QPointF(0, choice->height() / 2));
        const auto error = std::abs(selected.y() - field.y());
        report_["maxAlignmentError"] = std::max(report_["maxAlignmentError"].toDouble(), error);
        return error <= 1.0;
    }
    BackdropBridge& bridge_; QQmlApplicationEngine& engine_;
    QQuickWindow* window_{}; QQuickItem* short_{}; QQuickItem* long_{}; QJsonObject report_;
};

int main(int argc, char** argv) {
    const auto mode = qEnvironmentVariable("DANMAKU_ACRYLIC_MODE");
    // Must precede native window creation. Process-local and restricted to this D3D11 probe.
    if (mode == "NoRedirection" || mode == "Combined") qputenv("QT_QPA_DISABLE_REDIRECTION_SURFACE", "1");
    QQuickWindow::setDefaultAlphaBuffer(true);
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication app(argc, argv);
    app.setOrganizationName("DanmaXProbe"); app.setApplicationName("AcrylicPopupProbe");
    BackdropBridge bridge;
    QQmlApplicationEngine engine; engine.rootContext()->setContextProperty("bridge", &bridge);
    engine.load(QUrl("qrc:/probe/Main.qml"));
    if (engine.rootObjects().isEmpty()) return 1;
    bridge.setHost(qobject_cast<QQuickWindow*>(engine.rootObjects().first()));
    auto arguments = app.arguments();
    if (arguments.removeAll("--verify")) { ProbeTests tests(bridge, engine); return QTest::qExec(&tests, arguments); }
    return app.exec();
}
#include "main.moc"
