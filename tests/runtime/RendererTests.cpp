#include "renderer/DanmakuItem.h"
#include "renderer/TextTextureCache.h"
#include <QQuickWindow>
#include <QTest>

class RendererTests : public QObject {
    Q_OBJECT
  private slots:
    void budgetGrowthAndHysteresis() {
        constexpr auto MiB = TextureBudget::MiB;
        TextureBudget policy;
        QCOMPARE(policy.update(10 * MiB, true, 512 * MiB, 0), 64 * MiB);
        QCOMPARE(policy.update(65 * MiB, true, 512 * MiB, 1), 128 * MiB);
        QCOMPARE(policy.update(147 * MiB, true, 512 * MiB, 2), 256 * MiB);
        QCOMPARE(policy.update(260 * MiB, true, 512 * MiB, 3), 512 * MiB);
        QCOMPARE(policy.update(10 * MiB, true, 512 * MiB, 100), 512 * MiB);
        QCOMPARE(policy.update(10 * MiB, true, 512 * MiB, 10099), 512 * MiB);
        QCOMPARE(policy.update(10 * MiB, true, 512 * MiB, 10100), 64 * MiB);
        QCOMPARE(policy.update(1000 * MiB, true, 96 * MiB, 10101), 96 * MiB);
        QCOMPARE(policy.update(0, false, 32 * MiB, 10102), 32 * MiB);
        QCOMPARE(policy.update(0, false, 160 * MiB, 10103), 160 * MiB);
        // A new burst cancels a pending automatic shrink, including timestamp zero.
        TextureBudget burst;
        QCOMPARE(burst.update(100 * MiB, true, 512 * MiB, 0), 128 * MiB);
        QCOMPARE(burst.update(1 * MiB, true, 512 * MiB, 0), 128 * MiB);
        QCOMPARE(burst.update(100 * MiB, true, 512 * MiB, 9999), 128 * MiB);
        QCOMPARE(burst.update(1 * MiB, true, 512 * MiB, 10000), 128 * MiB);
        QCOMPARE(burst.update(1 * MiB, true, 512 * MiB, 20000), 64 * MiB);
    }

    void partialScrollLaneIsClipped() {
        QQuickWindow window;
        window.setColor(Qt::transparent);
        window.resize(400, 180);
        DanmakuItem renderer(window.contentItem());
        renderer.configure({{"fontFamily", "Microsoft YaHei"}, {"fontSize", 28},
                            {"strokeWidth", 1}, {"lineSpacing", .2}, {"opacity", 1.0}});
        const double step = renderer.trackHeight();
        const int viewportHeight = static_cast<int>(std::ceil(step * 2 + step * .6));
        renderer.setSize(QSizeF(400, viewportHeight));
        QVERIFY(renderer.clip());
        danmaku::Options options;
        options.trackHeight = step; options.maxTracks = 60; options.speed = 100;
        danmaku::Engine engine;
        engine.configure(options, 100, viewportHeight);
        engine.load({{0, danmaku::Mode::Scroll, "ABC"}, {0, danmaku::Mode::Scroll, "ABC"},
                     {0, danmaku::Mode::Scroll, "ABC"}});
        engine.tick(0, 0, true, [&](const auto& item) { return renderer.measure(item); });
        QCOMPARE(engine.activeCount(), std::size_t(3));
        const auto& last = engine.activeSlots()[engine.activeIndices().back()];
        QVERIFY(last.y < viewportHeight && last.y + last.height > viewportHeight);
        engine.tick(1, 1, true, [&](const auto& item) { return renderer.measure(item); });
        renderer.present(engine);
        window.show();
        QTRY_COMPARE(renderer.sceneEntries(), 3);
        QTRY_COMPARE(renderer.imageNodeCount(), 3);
        const auto image = window.grabWindow().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(!image.isNull());
        const double dpr = window.effectiveDevicePixelRatio();
        int visible = 0, outside = 0;
        for (int y = static_cast<int>(std::ceil(last.y * dpr)); y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                if (!qAlpha(image.pixel(x, y))) continue;
                if (y < static_cast<int>(std::ceil(viewportHeight * dpr))) ++visible;
                else ++outside;
            }
        QVERIFY(visible > 0);
        QCOMPARE(outside, 0);
        const auto artifacts = qEnvironmentVariable("DANMAKU_RENDERER_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) QVERIFY(image.save(artifacts + "-partial-scroll.png"));
    }

    void colorsOutlineAndTransparency_data() {
        QTest::addColumn<int>("stroke");
        QTest::newRow("no-outline") << 0;
        QTest::newRow("default-outline") << 1;
        QTest::newRow("wide-outline") << 3;
    }
    void colorsOutlineAndTransparency() {
        QFETCH(int, stroke);
        QQuickWindow window;
        window.setColor(Qt::transparent);
        window.resize(800, 400);
        DanmakuItem renderer(window.contentItem());
        renderer.setSize(QSizeF(window.size()));
        renderer.configure({{"fontFamily", "Microsoft YaHei"}, {"fontSize", 28},
                            {"strokeWidth", stroke}, {"lineSpacing", .2}, {"opacity", 1.0}});
        danmaku::Engine engine;
        engine.configure({}, 800, 400);
        engine.load({{0, danmaku::Mode::Top, "RED ABC", 0xff0000},
                     {0, danmaku::Mode::Top, "GREEN xyz", 0x00ff00},
                     {0, danmaku::Mode::Bottom, QStringLiteral("中文 😀 e\u0301").toUtf8().toStdString(), 0xffffff}});
        engine.tick(0, 0, true, [&](const auto& item) { return renderer.measure(item); });
        renderer.present(engine);
        window.show();
        QTRY_COMPARE(renderer.sceneEntries(), 3);
        QElapsedTimer preparation;
        preparation.start();
        while (renderer.imageNodeCount() < 3 && preparation.elapsed() < 3000) {
            renderer.present(engine);
            QTest::qWait(20);
        }
        QCOMPARE(renderer.imageNodeCount(), 3);
        const auto image = window.grabWindow().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(!image.isNull());
        int red = 0, green = 0, black = 0, transparent = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const auto pixel = image.pixel(x, y);
                if (qAlpha(pixel) == 0) ++transparent;
                else {
                    if (qRed(pixel) > 100 && qGreen(pixel) < 30) ++red;
                    if (qGreen(pixel) > 100 && qRed(pixel) < 30) ++green;
                    if (qRed(pixel) < 10 && qGreen(pixel) < 10 && qBlue(pixel) < 10) ++black;
                }
            }
        QVERIFY(red > 0);
        QVERIFY(green > 0);
        QVERIFY(transparent > 0);
        if (stroke) QVERIFY(black > 0);
        const auto artifacts = qEnvironmentVariable("DANMAKU_RENDERER_TEST_ARTIFACTS");
        if (!artifacts.isEmpty())
            QVERIFY(image.save(artifacts + QString("-stroke%1.png").arg(stroke)));
    }

    void progressivePreparationAndRetirement() {
        QQuickWindow window;
        window.resize(2560, 1440);
        DanmakuItem renderer(window.contentItem());
        renderer.setSize(QSizeF(window.size()));
        QVariantMap settings{{"fontFamily", "Microsoft YaHei"}, {"fontSize", 24},
                             {"strokeWidth", 1}, {"lineSpacing", .2}, {"opacity", 1.0},
                             {"textureBudgetAuto", false}, {"textureBudgetMiB", 64}};
        renderer.configure(settings);
        QVERIFY(renderer.imageBackend());
        danmaku::Options options;
        options.maxActive = 100;
        options.maxTracks = 40;
        options.overlap = true;
        options.fixedSeconds = 30;
        danmaku::Engine engine;
        engine.configure(options, window.width(), window.height());
        std::vector<danmaku::Item> items;
        for (int i = 0; i < 100; ++i)
            items.push_back({0, danmaku::Mode::Top,
                (QString(40, QChar(0x5f39)) + QString::number(i)).toUtf8().toStdString(), 0xffffff});
        engine.load(std::move(items));
        engine.tick(0, 0, true, [&](const auto& item) { return renderer.measure(item); });
        renderer.present(engine);
        window.show();
        QTRY_COMPARE(renderer.sceneEntries(), 100);
        // The cold frame cannot upload the whole burst. Deferred text remains visible.
        QVERIFY(renderer.imageNodeCount() > 0);
        QVERIFY(renderer.imageNodeCount() < 100);
        const auto coldCount = renderer.imageNodeCount();
        const bool native = QGuiApplication::platformName() == "windows";
        const auto id = renderer.firstSnapshotId();
        const auto x = renderer.firstSnapshotX();
        QElapsedTimer warmup;
        warmup.start();
        while (((native && renderer.imageNodeCount() < 100) || renderer.imageNodeCount() <= coldCount ||
                renderer.textureBytes() <= TextTextureCache::maxUploadBytes) && warmup.elapsed() < 10000) {
            renderer.present(engine);
            QTest::qWait(20);
        }
        QVERIFY(renderer.imageNodeCount() > coldCount);
        if (native) QCOMPARE(renderer.imageNodeCount(), 100);
        QCOMPARE(renderer.firstSnapshotId(), id);
        QCOMPARE(renderer.firstSnapshotX(), x);
        QVERIFY(renderer.textureBytes() > TextTextureCache::maxUploadBytes);
        QVERIFY(renderer.textureBytes() <= renderer.textureBudgetBytes());

        // New appearances replace the old resource budget rather than keeping stale textures.
        settings["strokeWidth"] = 3;
        settings["fontSize"] = 64;
        renderer.configure(settings);
        engine.reconfigure(options, window.width(), window.height(),
                           [&](const auto& item) { return renderer.measure(item); });
        renderer.present(engine);
        QTest::qWait(100);
        QVERIFY(renderer.textureBytes() <= renderer.textureBudgetBytes());
        QCOMPARE(renderer.firstSnapshotId(), id);

        // Saturate the real cache cap with unique live textures; excess entries stay as text.
        settings["strokeWidth"] = 0;
        renderer.configure(settings);
        options.maxActive = 400;
        engine.configure(options, window.width(), window.height());
        items.clear();
        for (int i = 0; i < 400; ++i)
            items.push_back({0, danmaku::Mode::Top,
                (QString(40, QChar(0x5f39)) + QString::number(i)).toUtf8().toStdString(), 0xffffff});
        engine.load(std::move(items));
        engine.tick(0, 0, true, [&](const auto& item) { return renderer.measure(item); });
        renderer.present(engine);
        QTRY_COMPARE(renderer.sceneEntries(), 400);
        warmup.restart();
        const auto initialBytes = renderer.textureBytes();
        while (((native && (renderer.textureBytes() < 60 * 1024 * 1024 ||
                            renderer.textureFallbackReasons()["capacity"].toInt() == 0)) ||
                renderer.textureBytes() <= initialBytes ||
                renderer.textureBytes() <= TextTextureCache::maxUploadBytes) && warmup.elapsed() < 10000) {
            renderer.present(engine);
            QTest::qWait(20);
        }
        QVERIFY(renderer.textureBytes() > initialBytes);
        if (native) QVERIFY(renderer.textureBytes() >= 60 * 1024 * 1024);
        QVERIFY(renderer.textureBytes() <= renderer.textureBudgetBytes());
        QVERIFY(renderer.imageNodeCount() < renderer.snapshotCount());
        if (native) QVERIFY(renderer.textureFallbackReasons()["capacity"].toInt() > 0);
        const auto currentId = renderer.firstSnapshotId();
        const auto currentX = renderer.firstSnapshotX();
        const auto currentLayouts = renderer.cacheMisses();
        settings["textureBudgetMiB"] = 32;
        renderer.configure(settings);
        renderer.present(engine);
        QTRY_COMPARE(renderer.textureBudgetBytes(), qint64(32 * TextureBudget::MiB));
        QVERIFY(renderer.textureBytes() <= renderer.textureBudgetBytes());
        QCOMPARE(renderer.firstSnapshotId(), currentId);
        QCOMPARE(renderer.firstSnapshotX(), currentX);
        QCOMPARE(renderer.cacheMisses(), currentLayouts);
        QCOMPARE(renderer.sceneEntries(), 400);
        settings["textureBudgetAuto"] = true;
        settings["textureBudgetMiB"] = 128;
        renderer.configure(settings);
        renderer.present(engine);
        QTRY_COMPARE(renderer.textureBudgetBytes(), qint64(128 * TextureBudget::MiB));
        const auto reducedBytes = renderer.textureBytes();
        warmup.restart();
        while (renderer.textureBytes() <= reducedBytes && warmup.elapsed() < 10000) {
            renderer.present(engine);
            QTest::qWait(20);
        }
        QVERIFY(renderer.textureBytes() > reducedBytes);
        QVERIFY(renderer.textureBytes() <= renderer.textureBudgetBytes());
        QCOMPARE(renderer.firstSnapshotId(), currentId);
        QCOMPARE(renderer.firstSnapshotX(), currentX);
        engine.unload();
        renderer.clearContent();
        QTRY_COMPARE(renderer.sceneEntries(), 0);
        QCOMPARE(renderer.textureBytes(), qint64(0));
        QCOMPARE(renderer.textureBudgetBytes(), qint64(0));
        QCOMPARE(renderer.textureDemandBytes(), qint64(0));
    }

    void sharedTexturesAndOversizeFallback() {
        QQuickWindow window;
        window.resize(800, 600);
        DanmakuItem renderer(window.contentItem());
        renderer.setSize(QSizeF(window.size()));
        renderer.configure({{"fontFamily", "Microsoft YaHei"}, {"fontSize", 24},
                            {"strokeWidth", 1}, {"lineSpacing", .2}, {"opacity", 1.0}});
        danmaku::Options options;
        options.overlap = true;
        danmaku::Engine engine;
        engine.configure(options, 800, 600);
        const auto duplicate = QStringLiteral("中文 A 😀 e\u0301").toUtf8().toStdString();
        engine.load({{0, danmaku::Mode::Top, duplicate, 0xff4040}});
        engine.tick(0, 0, true, [&](const auto& item) { return renderer.measure(item); });
        renderer.present(engine);
        window.show();
        QTRY_COMPARE(renderer.imageNodeCount(), 1);
        const auto bytes = renderer.textureBytes();
        engine.load({{0, danmaku::Mode::Top, duplicate, 0xff4040},
                     {0, danmaku::Mode::Top, duplicate, 0xff4040},
                     {0, danmaku::Mode::Top, QString(1000, QChar(0x5f39)).toUtf8().toStdString(), 0xffffff}});
        engine.tick(0, 0, true, [&](const auto& item) { return renderer.measure(item); });
        renderer.present(engine);
        QTRY_COMPARE(renderer.sceneEntries(), 3);
        QTRY_COMPARE(renderer.imageNodeCount(), 2);
        QCOMPARE(renderer.textureBytes(), bytes);
        QCOMPARE(renderer.textureDemandBytes(), bytes);
        QCOMPARE(renderer.textureFallbackReasons()["size"].toInt(), 1);
        const auto image = window.grabWindow();
        QVERIFY(!image.isNull());
        const auto artifacts = qEnvironmentVariable("DANMAKU_RENDERER_TEST_ARTIFACTS");
        if (!artifacts.isEmpty()) QVERIFY(image.save(artifacts + "-fallback.png"));
        engine.unload();
        renderer.clearContent();
        QTRY_COMPARE(renderer.textureBytes(), qint64(0));
    }
};

QTEST_MAIN(RendererTests)
#include "RendererTests.moc"
