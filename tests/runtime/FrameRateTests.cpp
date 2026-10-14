#include "application/AppController.h"
#include <QGuiApplication>
#include <QQuickWindow>
#include <QScreen>
#include <atomic>
#include <QTemporaryDir>
#include <QTest>
#include <cmath>
#include <thread>

class FrameRateTests : public QObject {
    Q_OBJECT
  private slots:
    void feedbackBuffer() {
        FrameFeedback feedback;
        std::vector<FrameFeedback::Sample> samples;
        samples.reserve(FrameFeedback::capacity);
        feedback.setActivity(true, true);
        for (std::size_t i = 0; i < FrameFeedback::capacity + 3; ++i) feedback.record(i);
        feedback.take(samples);
        QCOMPARE(samples.size(), FrameFeedback::capacity);
        QCOMPARE(samples.front().at, 3);
        QCOMPARE(samples.back().flags, 3u);
        QCOMPARE(feedback.total(), FrameFeedback::capacity + 3);
        QCOMPARE(feedback.lost(), 3);
        feedback.setActivity(false, false);
        feedback.record(5000);
        feedback.take(samples);
        QCOMPARE(samples.size(), 1);
        QCOMPARE(samples.front().flags, 0u);
        feedback.record(6000);
        feedback.discard();
        feedback.take(samples);
        QVERIFY(samples.empty());
        // The render callback owns its buffer even after GUI teardown.
        auto shared = std::make_shared<FrameFeedback>();
        std::thread producer([owned = shared] {
            for (int i = 0; i < 2000; ++i) owned->record(i);
        });
        std::size_t taken = 0;
        while (shared->total() < 2000) {
            shared->take(samples);
            taken += samples.size();
            std::this_thread::yield();
        }
        producer.join();
        shared->take(samples);
        QCOMPARE(taken + samples.size(), 2000);
        QCOMPARE(shared->lost(), 0);
    }
    void cadenceAndLifecycle() {
        QTemporaryDir directory;
        AppController controller(directory.path());
        // Keep Qt's diagnostics visible to the test runner, including failures
        // emitted on the native render thread before GUI queued logs are read.
        LogModel::uninstall();
        auto* settings = qobject_cast<SettingsStore*>(controller.settings());
        auto* monitor = controller.findChild<MediaMonitor*>();
        QObject::disconnect(monitor, nullptr, &controller, nullptr);
        QQuickWindow overlay;
        DanmakuItem renderer;
        renderer.setParentItem(overlay.contentItem());
        controller.attach(&renderer, &overlay);
        controller.beginDemo(50);
        renderer.setSize(overlay.size());
        overlay.show();
        QVERIFY(QTest::qWaitForWindowExposed(&overlay));
        const auto swaps = std::make_shared<std::atomic<int>>(0);
        connect(&overlay, &QQuickWindow::frameSwapped, this, [swaps] { swaps->fetch_add(1); }, Qt::DirectConnection);
        QTest::qWait(400);
        QVERIFY(settings->setValue("frameRateLimit", 30));
        QTest::qWait(100);
        // Simulate the shared animation driver's unsolicited window updates.
        QTimer unrelatedAnimation;
        unrelatedAnimation.setInterval(5);
        connect(&unrelatedAnimation, &QTimer::timeout, &overlay, &QQuickWindow::update);
        unrelatedAnimation.start();
        swaps->store(0);
        const double before = controller.position();
        QTest::qWait(1000);
        QVERIFY2(swaps->load() >= 22 && swaps->load() <= 34, qPrintable(QString("30 FPS swaps: %1").arg(swaps->load())));
        QVERIFY(std::abs(controller.position() - before - 1) < .15);
        controller.togglePause();
        QTest::qWait(100);
        swaps->store(0);
        const double x = renderer.firstSnapshotX();
        const auto id = renderer.firstSnapshotId();
        const double frozen = controller.position();
        QTest::qWait(200);
        QVERIFY(swaps->load() <= 1);
        QCOMPARE(controller.position(), frozen);
        QCOMPARE(renderer.firstSnapshotX(), x);
        QVERIFY(settings->setValue("fontSize", 30));
        QTest::qWait(100);
        QVERIFY(swaps->load() >= 1);
        QCOMPARE(renderer.firstSnapshotId(), id);
        QCOMPARE(renderer.firstSnapshotFontSize(), 30.0);
        QCOMPARE(controller.position(), frozen);
        swaps->store(0);
        QVERIFY(settings->setValue("targetFrameRate", 90));
        QTest::qWait(100);
        QVERIFY(swaps->load() >= 1);
        swaps->store(0);
        QTest::qWait(150);
        QVERIFY(swaps->load() <= 1);
        QCOMPARE(controller.position(), frozen);
        controller.togglePause();
        QTest::qWait(100);
        swaps->store(0);
        QTest::qWait(400);
        QVERIFY(swaps->load() >= 8 && swaps->load() <= 15);
        overlay.hide();
        QTest::qWait(100);
        swaps->store(0);
        const double hidden = controller.position();
        QTest::qWait(200);
        QVERIFY(swaps->load() <= 1);
        QVERIFY(controller.position() > hidden + .1);
        overlay.show();
        QTest::qWait(100);
        QVERIFY(settings->setValue("frameRateLimit", 15));
        QTest::qWait(100);
        swaps->store(0);
        QTest::qWait(800);
        QVERIFY2(swaps->load() >= 8 && swaps->load() <= 15, qPrintable(QString("15 FPS swaps: %1").arg(swaps->load())));
        unrelatedAnimation.stop();

        // Continuous media sampling must not become a second source of frame requests.
        controller.stop();
        controller.selectSession("injected-frames");
        controller.start(false);
        auto inject = [&](double position, double rate, bool playing, const QString& identity = "one") {
            MediaSample sample;
            sample.found = true; sample.id = "injected-frames"; sample.identity = identity;
            sample.position = position; sample.duration = 100; sample.rate = rate;
            sample.generation = monitor->generation(); sample.observedAt = MediaMonitor::monotonicSeconds();
            sample.playback = playing ? MediaSample::Playback::Playing : MediaSample::Playback::Paused;
            return QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
        };
        QVERIFY(inject(0, 2, true));
        QTest::qWait(100);
        swaps->store(0);
        const double mediaBefore = controller.position();
        const double start = MediaMonitor::monotonicSeconds();
        for (int i = 0; i < 100; ++i) {
            QVERIFY(inject(mediaBefore + (MediaMonitor::monotonicSeconds() - start) * 2, 2, true));
            QTest::qWait(5);
        }
        QVERIFY2(swaps->load() <= 12, qPrintable(QString("Sample flood bypassed cap: %1").arg(swaps->load())));
        QVERIFY(controller.position() > mediaBefore + .8);
        QVERIFY(inject(0, 1, false, "two"));
        QCOMPARE(controller.position(), 0.0);
        QVERIFY(!controller.playing());
        controller.stop();
        QTest::qWait(100);
        swaps->store(0);
        QTest::qWait(150);
        QCOMPARE(swaps->load(), 0);
    }
};
QTEST_MAIN(FrameRateTests)
#include "FrameRateTests.moc"
