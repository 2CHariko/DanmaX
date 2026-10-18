#include "platform/windows/PlaybackPresentationController.h"
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTest>

class PlaybackPresentationTests final : public QObject {
    Q_OBJECT
private slots:
    void fullscreenVisibilityAndRestore() {
        QQuickWindow main, controls;
        controls.setFlags(Qt::Tool | Qt::FramelessWindowHint); controls.setTransientParent(&main); controls.resize(640, 100);
        main.setGeometry(120, 100, 800, 600); main.show();
        bool foreground = true;
        PlaybackPresentationController presentation(nullptr, 80, [&](QWindow*) { return foreground; }, [] { return QPoint(0, 0); });
        presentation.attach(&main, &controls, nullptr);
        presentation.setPlaying(true); presentation.toggleFullscreen(); QVERIFY(!presentation.fullscreen());
        presentation.setWatching(true);
        const auto handle = main.winId(); const auto geometry = main.geometry();
        QSignalSpy changes(&presentation, &PlaybackPresentationController::aboutToChangeHost);
        presentation.toggleFullscreen(); QVERIFY(presentation.fullscreen());
        QTRY_COMPARE(main.windowState(), Qt::WindowFullScreen);
        QCOMPARE(main.winId(), handle);
        QTRY_VERIFY(!presentation.controlsVisible()); QVERIFY(!controls.isVisible());
        presentation.setPlaying(false); QTRY_VERIFY(presentation.controlsVisible());
        QTest::qWait(160); QVERIFY(presentation.controlsVisible());
        presentation.setPlaying(true); presentation.setBlocked(true); QTest::qWait(160); QVERIFY(presentation.controlsVisible());
        presentation.setBlocked(false); QTRY_VERIFY(!presentation.controlsVisible());
        foreground = false; presentation.sample(); QVERIFY(!presentation.controlsVisible());
        foreground = true; presentation.sample(); QVERIFY(presentation.controlsVisible());
        QQuickWindow dialog; dialog.setFlags(Qt::Dialog); dialog.setTransientParent(&main); dialog.show();
        QTest::qWait(160); presentation.sample(); QVERIFY(presentation.controlsVisible());
        QVERIFY(!presentation.handleKey(Qt::Key_Escape)); QVERIFY(presentation.fullscreen()); dialog.hide();
        QVERIFY(presentation.handleKey(Qt::Key_Escape)); QVERIFY(!presentation.fullscreen());
        QTRY_COMPARE(main.windowState(), Qt::WindowNoState); QTRY_COMPARE(main.geometry(), geometry); QCOMPARE(main.winId(), handle);
        QCOMPARE(changes.count(), 2); QVERIFY(!controls.isVisible());
        presentation.toggleFullscreen(); presentation.setWatching(false); QVERIFY(!presentation.fullscreen());
        QVERIFY(!presentation.handleKey(Qt::Key_F11));
    }
    void maximizedAndKeyboard() {
        QQuickWindow main, controls; main.setGeometry(100, 100, 720, 520); main.show();
        controls.setFlags(Qt::Tool | Qt::FramelessWindowHint); controls.setTransientParent(&main); controls.resize(640, 100);
        PlaybackPresentationController presentation(nullptr, 80, [](QWindow*) { return true; }, [] { return QPoint(0, 0); });
        presentation.attach(&main, &controls, nullptr); presentation.setWatching(true);
        main.showMaximized(); QTRY_COMPARE(main.windowState(), Qt::WindowMaximized);
        QVERIFY(presentation.handleKey(Qt::Key_F11)); QVERIFY(presentation.fullscreen());
        QSignalSpy focus(&presentation, &PlaybackPresentationController::keyboardControlsRequested);
        QVERIFY(presentation.handleKey(Qt::Key_Tab)); QCOMPARE(focus.size(), 1);
        presentation.exitFullscreen(); QTRY_COMPARE(main.windowState(), Qt::WindowMaximized);
    }
};
int main(int argc, char** argv) { QGuiApplication app(argc, argv); PlaybackPresentationTests tests; return QTest::qExec(&tests, argc, argv); }
#include "PlaybackPresentationTests.moc"
