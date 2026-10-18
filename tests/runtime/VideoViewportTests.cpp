#include "platform/windows/VideoViewportController.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTest>
#include <windows.h>

namespace {
RECT rectangle(QWindow* window) {
    RECT result{};
    if (window->handle()) GetWindowRect(reinterpret_cast<HWND>(window->winId()), &result);
    return result;
}
bool aligned(QWindow* a, QWindow* b) {
    const auto x = rectangle(a), y = rectangle(b);
    return EqualRect(&x, &y);
}
struct View {
    QQuickWindow host;
    QQuickItem item{host.contentItem()};
    QWindow overlay;
    VideoViewportController viewport;
    View() {
        host.setGeometry(150, 150, 640, 400);
        item.setPosition({11.5, 23.5}); item.setSize({600.5, 330.5});
        overlay.setFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus);
        viewport.videoWindow()->setParent(&host);
        viewport.videoWindow()->setGeometry(12, 24, 601, 331);
        viewport.attach(&item); viewport.setOverlay(&overlay);
        host.show(); overlay.show(); viewport.setActive(true);
        viewport.synchronize();
    }
    void moveNative(int dx, int dy) {
        const auto r = rectangle(&host);
        SetWindowPos(reinterpret_cast<HWND>(host.winId()), nullptr, r.left + dx, r.top + dy, 0, 0,
            SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }
};
}
class VideoViewportTests final : public QObject {
    Q_OBJECT
private slots:
    void movesFollowBeforeReturningWithoutEventPump() {
        View view;
        QTRY_VERIFY(view.viewport.followMetrics().value("hooked").toBool());
        const auto videoId = view.viewport.nativeHandle(), overlayId = view.overlay.winId();
        for (int i = 0; i < 20; ++i) {
            view.moveNative(i % 2 ? -17 : 29, i % 2 ? 11 : -7);
            QVERIFY(aligned(view.viewport.videoWindow(), &view.overlay));
        }
        auto* video = view.viewport.videoWindow();
        // A video-only size/position change is also observed synchronously.
        QVERIFY(SetWindowPos(reinterpret_cast<HWND>(videoId), nullptr, 13, 27, 550, 300, SWP_NOZORDER | SWP_NOACTIVATE));
        QVERIFY(aligned(video, &view.overlay));
        QCOMPARE(view.overlay.size(), video->size());
        const auto writes = view.viewport.followMetrics().value("nativeCorrections").toULongLong();
        for (int i = 0; i < 100; ++i) view.viewport.synchronize();
        QCOMPARE(view.viewport.followMetrics().value("nativeCorrections").toULongLong(), writes);
        QCOMPARE(view.viewport.nativeHandle(), videoId); QCOMPARE(view.overlay.winId(), overlayId);
        const auto metrics = view.viewport.followMetrics();
        QVERIFY(metrics.value("nativeEvents").toULongLong() >= 20);
        QCOMPARE(metrics.value("failures").toULongLong(), 0);
        QCOMPARE(metrics.value("maxAfterEventPx").toInt(), 0);
    }
    void hidingAndHandoffDoNotReclaimSharedOverlay() {
        View view;
        QTRY_VERIFY(view.viewport.followMetrics().value("hooked").toBool());
        view.item.setVisible(false);
        QVERIFY(!view.viewport.followMetrics().value("hooked").toBool());
        const QRect desktop(0, 0, 900, 600);
        view.overlay.setTransientParent(nullptr); view.overlay.setGeometry(desktop);
        view.moveNative(30, 40); view.viewport.synchronize();
        QCOMPARE(view.overlay.geometry(), desktop); QVERIFY(!view.overlay.transientParent());
        view.item.setVisible(true); view.viewport.setActive(false);
        view.overlay.setTransientParent(nullptr); view.overlay.setGeometry(desktop);
        view.moveNative(-30, -40); QCoreApplication::processEvents();
        QCOMPARE(view.overlay.geometry(), desktop); QVERIFY(!view.overlay.transientParent());
        QVERIFY(!view.viewport.followMetrics().value("hooked").toBool());
        view.viewport.setActive(true); view.viewport.synchronize();
        QTRY_VERIFY(view.viewport.followMetrics().value("hooked").toBool());
        QVERIFY(aligned(view.viewport.videoWindow(), &view.overlay));
    }
    void replacementAndDestroyedSurfacesDoNotReceiveOldWrites() {
        View view;
        QTRY_VERIFY(view.viewport.followMetrics().value("hooked").toBool());
        QWindow replacement;
        replacement.setFlags(view.overlay.flags()); replacement.show();
        view.viewport.setOverlay(&replacement);
        const auto old = view.overlay.geometry();
        view.moveNative(31, 9);
        QCOMPARE(view.overlay.geometry(), old);
        QVERIFY(aligned(view.viewport.videoWindow(), &replacement));
        // No media is bound in this test: surface recreation is intentional.
        replacement.destroy();
        QVERIFY(!view.viewport.followMetrics().value("hooked").toBool());
        view.moveNative(-17, -13);
        QVERIFY(!replacement.handle()); // Following must not call winId() here.
        replacement.show(); view.viewport.synchronize();
        QTRY_VERIFY(view.viewport.followMetrics().value("hooked").toBool());
        view.moveNative(11, 3); QVERIFY(aligned(view.viewport.videoWindow(), &replacement));
        view.viewport.setOverlay(nullptr);
        const auto rect = replacement.geometry(); view.moveNative(-11, -3);
        QCOMPARE(replacement.geometry(), rect);
    }
    void minimizeRestoreAndControllerDestruction() {
        View view;
        QTRY_VERIFY(view.viewport.followMetrics().value("hooked").toBool());
        view.host.showMinimized(); view.viewport.synchronize();
        QVERIFY(!view.viewport.shown()); QVERIFY(!view.viewport.followMetrics().value("hooked").toBool());
        view.host.showNormal(); view.viewport.synchronize();
        QTRY_VERIFY(view.viewport.followMetrics().value("hooked").toBool());
        view.moveNative(15, 0); QVERIFY(aligned(view.viewport.videoWindow(), &view.overlay));
        view.viewport.setActive(false);
        // The host outlives this second controller; its subclass must be gone.
        {
            VideoViewportController other;
            other.videoWindow()->setParent(&view.host); other.videoWindow()->setGeometry(0, 0, 300, 200);
            other.attach(&view.item); other.setOverlay(&view.overlay); other.setActive(true);
            QTRY_VERIFY(other.followMetrics().value("hooked").toBool());
        }
        const auto rect = view.overlay.geometry(); view.moveNative(-15, 0);
        QCOMPARE(view.overlay.geometry(), rect);
    }
};
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    VideoViewportTests tests; return QTest::qExec(&tests, argc, argv);
}
#include "VideoViewportTests.moc"
