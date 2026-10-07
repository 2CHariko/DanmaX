#include "application/AppController.h"
#include "infrastructure/SettingsStore.h"
#include "infrastructure/XmlLoader.h"
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QGuiApplication>
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <QTemporaryDir>
#include <QTimer>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QThread>
#include <QImage>
#include <QLocale>
#include <iostream>
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    // Optional read-only probe for a real hosted player window; no playback or focus changes.
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "--window-match") {
        bool ok=false;
        const auto handle=QString::fromLocal8Bit(argv[2]).toULongLong(&ok);
        const bool matched=ok && MediaMonitor::windowMatchesSession(static_cast<quintptr>(handle),QString::fromLocal8Bit(argv[3]));
        std::cout << (matched ? "matched" : "not matched") << '\n';
        return matched ? 0 : 1;
    }
    QTemporaryDir dir;
    int failed = 0;
    auto check = [&](bool v, const char* message) {
        if (!v) {
            std::cerr << message << '\n';
            ++failed;
        }
    };
    {
        const HWND window = CreateWindowExW(0, L"STATIC", L"Window identity test", 0, 0, 0, 1, 1, nullptr,
                                            nullptr, GetModuleHandleW(nullptr), nullptr);
        check(window != nullptr, "Create hidden identity test window");
        IPropertyStore* properties = nullptr;
        if (window && SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(&properties)))) {
            wchar_t appId[] = L"Test.Player_family!Player";
            PROPVARIANT value{};
            value.vt = VT_LPWSTR;
            value.pwszVal = appId;
            check(SUCCEEDED(properties->SetValue(PKEY_AppUserModel_ID, value)), "Set hosted window AUMID");
            check(MediaMonitor::windowMatchesSession(reinterpret_cast<quintptr>(window),
                                                     "Test.Player_family!Player"),
                  "Match hosted player by window AUMID");
            check(!MediaMonitor::windowMatchesSession(reinterpret_cast<quintptr>(window),
                                                      "Other.Player_family!Player"),
                  "Reject another hosted application");
            check(MediaMonitor::windowMatchesSession(reinterpret_cast<quintptr>(window), "runtime_tests.exe"),
                  "Preserve desktop executable matching when shell app ID differs");
            properties->Release();
        } else
            check(false, "Access window property store");
        if (window)
            DestroyWindow(window);
        check(!MediaMonitor::windowMatchesSession(0, "Test.Player_family!Player"), "Reject missing window");
    }
    auto write = [&](const QString& name, const QByteArray& bytes) {
        QFile f(dir.filePath(name));
        check(f.open(QIODevice::WriteOnly), "Create test fixture");
        check(f.write(bytes) == bytes.size(), "Write test fixture");
        return f.fileName();
    };
    const auto path = write(
        "sample.xml",
        R"(<i><d p="2,5,25,16777215">top</d><d p="0,1,25,255">hello &amp; world</d><d p="nan,1,25,0">bad</d><d p="1,7,25,0">unsupported</d><d p="1,4,25,-1">bad color</d></i>)");
    auto result = readDanmakuXml(path);
    check(result.error.isEmpty() && result.items.size() == 2 && result.skipped == 3, "XML filtering");
    check(result.items[0].time == 0 && result.items[0].text == "hello & world" &&
              result.items[0].color == 255 && result.items[0].mode == danmaku::Mode::Scroll &&
              result.items[1].mode == danmaku::Mode::Top && result.items[0].fontSize == 25,
          "XML sorting, entities, color and mode mapping");
    const auto sizes = readDanmakuXml(write("sizes.xml",
        R"(<i><d p="0,1,18,16777215">same</d><d p="0,5,36,16776960">same</d><d p="0,4,25,0">bottom</d><d p="0,1,,0">missing size</d><d p="0,1,0,0">zero</d><d p="0,1,nan,0">invalid</d><d p="0,1,201,0">huge</d></i>)"));
    check(sizes.error.isEmpty() && sizes.items.size() == 3 && sizes.skipped == 4,
          "XML validates source font size without accepting malformed records");
    check(sizes.items.size() == 3 && sizes.items[0].fontSize == 18 && sizes.items[1].fontSize == 36 &&
              sizes.items[2].mode == danmaku::Mode::Bottom,
          "XML preserves source font sizes, equal-time order and bottom mode");
    check(!readDanmakuXml(write("bad.xml", "<i><d>")).error.isEmpty(), "Malformed XML error");
    check(!readDanmakuXml(write("dtd.xml", "<!DOCTYPE i [<!ENTITY x 'x'>]><i/>")).error.isEmpty(),
          "Reject DTD");
    check(!readDanmakuXml(dir.filePath("missing.xml")).error.isEmpty(), "Missing file error");
    std::stop_source source;
    source.request_stop();
    check(readDanmakuXml(path, source.get_token()).cancelled, "Cooperative XML cancellation");
    {
        SettingsStore settings(dir.path());
        QFile initial(dir.filePath("settings.ini"));
        check(initial.open(QIODevice::ReadOnly), "Create INI on first launch");
        const auto annotated = initial.readAll();
        initial.close();
        check(QString::fromUtf8(annotated).contains(QStringLiteral("滚动弹幕速度")) &&
                  annotated.contains("[Meta]\r\n") && annotated.contains("formatVersion=1"),
              "First launch writes readable UTF-8 Chinese instructions");
        check(settings.setValue("speed", 300), "Save valid setting");
        check(!settings.setValue("speed", 0) && settings.values()["speed"].toInt() == 300,
              "Reject invalid setting without mutation");
        check(settings.retrySave(), "Flush numeric edits");
        SettingsStore restored(dir.path());
        check(restored.error().isEmpty() && restored.values()["speed"].toInt() == 300,
              "Restore persisted INI settings");
    }
    {
        const auto originalLocale = QLocale();
        QLocale::setDefault(QLocale(QLocale::German));
        QTemporaryDir roundtrip;
        const QStringList strings{
            QStringLiteral("D:\\弹幕\\测试😀,;=\"\\文件.xml"),
            QStringLiteral("@Variant(不应成为类型)"), QStringLiteral("@@前缀"),
            QStringLiteral("  前后空格  "), QStringLiteral("换行\n回车\r制表\t\\引号\""),
            QStringLiteral("控制") + QChar(0) + "7Af" + QChar(0x1f) + "aF9", QString()};
        for (const auto& value : strings) {
            QVariantMap expected;
            {
                SettingsStore settings(roundtrip.path());
                check(settings.setValue("lastFile", value), "Save quoted INI string");
                check(settings.setValue("targetSession", value), "Save arbitrary session ID");
                check(settings.setValue("opacity", 0.375), "Save C-locale decimal");
                check(settings.setValue("timeOffset", -0.125), "Save negative decimal");
                check(settings.setValue("overlap", true), "Save boolean");
                check(settings.setValue("speed", 300 + value.size()), "Queue numeric edit before destruction");
                expected = settings.values();
                // Numeric edits must also flush when the owner is destroyed.
            }
            SettingsStore restored(roundtrip.path());
            check(restored.error().isEmpty() && restored.values() == expected,
                  "Round trip Chinese, Unicode, escaping, typed prefixes and decimals");
        }
        SettingsStore settings(roundtrip.path());
        check(settings.reset(), "Reset INI settings");
        SettingsStore reset(roundtrip.path());
        check(reset.values() == SettingsStore::defaults(), "Reset persists complete defaults");
        QFile file(roundtrip.filePath("settings.ini"));
        check(file.open(QIODevice::ReadOnly), "Read saved annotations");
        const auto bytes = file.readAll();
        check(bytes.contains("opacity=0.85") &&
                  QString::fromUtf8(bytes).contains(QStringLiteral("暂停时间不计入寿命")),
              "Saving and resetting retain Chinese comments and locale-neutral decimals");
        QLocale::setDefault(originalLocale);
    }
    auto configFixture = [&](const QString& directory, const QByteArray& bytes) {
        QFile file(QDir(directory).filePath("settings.ini"));
        check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Create INI fixture");
        check(file.write(bytes) == bytes.size(), "Write INI fixture");
    };
    {
        QTemporaryDir manual;
        configFixture(manual.path(), QStringLiteral(
            "\ufeff; 手工编辑配置\n[Meta]\nformatVersion=1\n"
            "[Appearance]\nfontFamily=\"微软雅黑\"\nopacity=0.6\n"
            "[Danmaku]\nspeed=240\nfixedSeconds=7.5\noverlap=TRUE\n"
            "[Window]\nforegroundOnly=1\n[Diagnostics]\ndebug=0\n"
            "[Logging]\nlogToFile=false\n").toUtf8());
        SettingsStore settings(manual.path());
        check(settings.error().isEmpty() && settings.values()["fontFamily"] == QStringLiteral("微软雅黑") &&
                  settings.values()["speed"] == 240 && settings.values()["fixedSeconds"] == 7.5 &&
                  settings.values()["overlap"].toBool() && settings.values()["foregroundOnly"].toBool() &&
                  !settings.values()["debug"].toBool() && !settings.values()["logToFile"].toBool() &&
                  settings.values()["maxActive"] == 500,
              "Read BOM, Chinese, hand-edited booleans and missing-field defaults");
    }
    {
        QTemporaryDir invalid;
        const QByteArray original = "[Meta]\nformatVersion=1\n[Appearance]\ntheme=blue\nfontFamily=a,b\n"
                                    "[Danmaku]\nspeed=0\nmaxTracks=1.5\nfixedSeconds=nan\noverlap=yes\n"
                                    "[Sync]\ntimeOffset=3.25\n";
        configFixture(invalid.path(), original);
        SettingsStore settings(invalid.path());
        auto expected = SettingsStore::defaults();
        expected["timeOffset"] = 3.25;
        check(!settings.error().isEmpty() && settings.values() == expected,
              "Reject invalid range, fractions, non-finite numbers, lists, enums and booleans per field");
        check(settings.retrySave() && settings.error().isEmpty(), "Recover invalid fields by atomic save");
        const auto backups = QDir(invalid.path()).entryList({"settings.ini.backup-*"});
        check(backups.size() == 1, "Preserve invalid original before first overwrite");
        if (!backups.isEmpty()) {
            QFile backup(invalid.filePath(backups.first()));
            check(backup.open(QIODevice::ReadOnly) && backup.readAll() == original,
                  "INI backup preserves original bytes");
        }
        SettingsStore restored(invalid.path());
        check(restored.error().isEmpty() && restored.values() == expected, "Recover valid fields without migration");
    }
    for (const auto& original : QList<QByteArray>{
             "[Meta]\nformatVersion=2\n[Danmaku]\nspeed=360\n",
             "[Danmaku]\nspeed=360\n", "[broken\nspeed=360\n",
             QByteArray("[Meta]\nformatVersion=1\n; ") + char(-1), QByteArray(1024 * 1024 + 1, 'x')}) {
        QTemporaryDir corrupt;
        configFixture(corrupt.path(), original);
        SettingsStore settings(corrupt.path());
        check(!settings.error().isEmpty() && settings.values() == SettingsStore::defaults(),
              "Reject malformed, missing-version, future-version, invalid UTF-8 and oversized INI");
        QFile file(corrupt.filePath("settings.ini"));
        check(file.open(QIODevice::ReadOnly) && file.readAll() == original,
              "Reading a damaged INI must not overwrite it");
        file.close();
        check(settings.setValue("speed", 200), "Recover damaged INI on explicit settings save");
        check(QDir(corrupt.path()).entryList({"settings.ini.backup-*"}).size() == 1,
              "Damaged INI has a backup after recovery");
    }
    {
        QTemporaryDir legacy;
        QFile json(legacy.filePath("settings.json"));
        check(json.open(QIODevice::WriteOnly), "Create ignored JSON fixture");
        const QByteArray original = "{\"version\":1,\"values\":{\"speed\":900}}";
        json.write(original);
        json.close();
        QFile oldIni(legacy.filePath("config.ini"));
        check(oldIni.open(QIODevice::WriteOnly), "Create ignored legacy INI fixture");
        oldIni.write("[Display]\nfont_size=50\n");
        oldIni.close();
        SettingsStore settings(legacy.path());
        check(settings.error().isEmpty() && settings.values() == SettingsStore::defaults(),
              "Breaking format change does not migrate JSON or legacy INI");
        check(json.open(QIODevice::ReadOnly) && json.readAll() == original,
              "Ignored old configuration is not deleted or modified");
    }
    {
        QTemporaryDir retry;
        const auto missing = retry.filePath("missing");
        SettingsStore settings(missing);
        check(!settings.error().isEmpty() && !settings.setValue("overlap", true),
              "Report unavailable configuration location");
        check(QDir().mkpath(missing) && settings.retrySave(), "Retry failed save after location becomes writable");
        SettingsStore restored(missing);
        check(restored.error().isEmpty() && restored.values()["overlap"].toBool(),
              "Retry persists pending values");
    }
    {
        AppController controller(dir.path());
        QEventLoop loop;
        bool success = false;
        QObject::connect(&controller, &AppController::loadCompleted, &loop, [&](bool ok) {
            success = ok;
            loop.quit();
        });
        auto recovered = QStringLiteral(
            "<i><d p=\"0,1,25,undefined,1,0,user,123\">中文😀</d>"
            "<d p=\"1,5,36,255\">top</d><d p=\"2,7,25,0\">advanced").toUtf8();
        recovered += char(0x16);
        recovered += "</d></i>";
        const auto recoveredPath = write("bilibili-recovered.xml", recovered);
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        controller.loadFile(recoveredPath);
        loop.exec();
        check(success && controller.total() == 2 && !controller.loading() && controller.error().isEmpty(),
              "Asynchronous Bilibili recovery reaches application state");
        check(controller.status().contains(QStringLiteral("不支持的模式 1")) &&
                  controller.status().contains(QStringLiteral("修复 1 个非法控制字符")) &&
                  controller.status().contains(QStringLiteral("1 条缺失颜色使用白色")),
              "Application reports unsupported modes, sanitation and fallback colors");
    }
    {
        AppController controller(dir.path());
        QEventLoop loop;
        bool success = false;
        int loads = 0;
        QObject::connect(&controller, &AppController::loadCompleted, &loop, [&](bool ok) {
            success = ok;
            ++loads;
            loop.quit();
        });
        QTimer::singleShot(10000, &loop, &QEventLoop::quit);
        controller.loadFile(path);
        loop.exec();
        check(success && controller.total() == 2 && !controller.loading(),
              "Asynchronous XML controller integration");
        LogModel log(dir.filePath("bounded-logs"));
        log.configure(false, "WARNING");
        log.append("INFO", "filtered");
        check(log.count() == 0, "Log level filtering");
        for (int i = 0; i < 1200; ++i)
            log.append("WARNING", QString::number(i));
        check(log.count() <= 1000, "Bounded log history");
        check(log.exportTo(dir.filePath("export.txt")), "Log export");
        std::atomic<int> sceneInvalidations{};
        std::atomic<bool> renderedOnWorker{};
        QQuickWindow overlay;
        QObject::connect(&overlay, &QQuickWindow::sceneGraphInvalidated, &controller,
                         [&] { ++sceneInvalidations; }, Qt::DirectConnection);
        QObject::connect(&overlay, &QQuickWindow::beforeRendering, &controller,
                         [&] { renderedOnWorker.store(QThread::currentThread() != app.thread()); },
                         Qt::DirectConnection);
        DanmakuItem renderer;
        controller.attach(&renderer, &overlay);
        auto appearance = SettingsStore::defaults();
        renderer.configure(appearance);
        danmaku::Item text{0, danmaku::Mode::Scroll, "cache check", 0xffffff};
        const auto firstWidth = renderer.measure(text).width;
        renderer.measure(text);
        check(renderer.cacheEntries() == 1 && renderer.cacheHits() > 0, "Text layout reuse");
        appearance["fontSize"] = 48;
        renderer.configure(appearance);
        check(renderer.cacheEntries() == 0 && renderer.measure(text).width > firstWidth,
              "Font change invalidates layout");
        const auto normal = renderer.measure(text);
        text.fontSize = 18;
        const auto smallExtent = renderer.measure(text);
        text.fontSize = 36;
        const auto large = renderer.measure(text);
        check(smallExtent.width < normal.width && smallExtent.height < normal.height &&
                  large.width > normal.width && large.height > normal.height && renderer.cacheEntries() == 3,
              "Source font size scales width and height and separates identical text layouts");
        text.fontSize = 25;
        danmaku::Engine scene;
        scene.configure({}, 800, 600);
        scene.load({text});
        scene.tick(0, 0, true, [&](const auto& item) { return renderer.measure(item); });
        renderer.present(scene);
        check(renderer.snapshotCount() == 1 && renderer.firstSnapshotId() != 0,
              "New comment is present in its first compact snapshot");
        const auto liveId = renderer.firstSnapshotId();
        appearance["strokeWidth"] = 3;
        const auto cached = renderer.cacheEntries();
        renderer.configure(appearance);
        check(renderer.snapshotCount() == 1 && renderer.firstSnapshotId() == liveId &&
                  renderer.cacheEntries() == cached,
              "Stroke edit retains the snapshot and reusable glyph layouts before replacement");
        scene.reconfigure({}, 800, 600, [&](const auto& item) { return renderer.measure(item); });
        renderer.present(scene);
        check(renderer.firstSnapshotId() == liveId, "Style update keeps live snapshot identity");
        scene.clear();
        renderer.present(scene);
        check(renderer.snapshotCount() == 0, "Clear retires snapshot and layout references");
        auto wait = [&](int ms) {
            QEventLoop delay;
            QTimer::singleShot(ms, &delay, &QEventLoop::quit);
            delay.exec();
        };
        renderer.setParentItem(overlay.contentItem());
        renderer.setSize(QSizeF(overlay.width(), overlay.height()));
        overlay.show();
        wait(300);
        if (qEnvironmentVariable("QSG_RHI_BACKEND") == "d3d11")
            check(overlay.rendererInterface()->graphicsApi() == QSGRendererInterface::Direct3D11,
                  "Native lifecycle probe uses the requested D3D11 backend");
        if (qEnvironmentVariable("QSG_RENDER_LOOP") == "threaded")
            check(renderedOnWorker.load(), "Native lifecycle probe uses a separate rendering thread");
        else if (qEnvironmentVariable("QSG_RENDER_LOOP") == "basic")
            check(!renderedOnWorker.load(), "Native lifecycle probe uses the GUI rendering thread");
        controller.start(true);
        wait(80);
        check(controller.position() > 0, "Manual clock advances");
        controller.togglePause();
        const auto paused = controller.position();
        wait(80);
        check(controller.position() == paused, "Manual pause freezes position");
        auto* liveSettings = qobject_cast<SettingsStore*>(controller.settings());
        const auto pausedId = renderer.firstSnapshotId();
        const auto pausedX = renderer.firstSnapshotX();
        const auto misses = renderer.cacheMisses();
        check(pausedId != 0, "Paused settings fixture has a live comment");
        liveSettings->setValue("speed", 400);
        liveSettings->setValue("fontSize", 28);
        liveSettings->setValue("fontSize", 30);
        liveSettings->setValue("fontSize", 36);
        liveSettings->setValue("strokeWidth", 2);
        liveSettings->setValue("lineSpacing", 0.4);
        liveSettings->setValue("maxTracks", 30);
        liveSettings->setValue("maxActive", 600);
        liveSettings->setValue("overlap", true);
        liveSettings->setValue("opacity", 0.6);
        wait(30);
        check(renderer.snapshotCount() == 1 && renderer.firstSnapshotId() == pausedId &&
                  renderer.firstSnapshotX() == pausedX && renderer.firstSnapshotFontSize() == 36 &&
                  controller.position() == paused && !controller.playing(),
              "Paused batch edits preserve the live comment and clock while updating appearance immediately");
        check(renderer.cacheMisses() == misses + 1,
              "Rapid font edits coalesce into one final live layout instead of measuring intermediate sizes");
        check(loads == 1 && controller.total() == 2 && !controller.loading(), "Settings changes never reload XML");
        controller.togglePause();
        wait(80);
        check(controller.position() > paused && controller.position() < paused + 0.5,
              "Resume restarts frame requests without counting paused time");
        check(renderer.snapshotCount() == 1 && renderer.firstSnapshotId() == pausedId &&
                  renderer.firstSnapshotX() < pausedX, "Updated settings keep the same comment moving on resume");
        controller.togglePause();
        controller.seek(1);
        check(controller.position() == 1, "Manual seek");
        controller.selectSession("test-session");
        // Isolate injected snapshots from real machine session discovery.
        QObject::disconnect(controller.findChild<MediaMonitor*>(), nullptr, &controller, nullptr);
        controller.start(false);
        MediaSample sample;
        sample.found = true;
        sample.id = "test-session";
        sample.identity = "one";
        sample.position = 42;
        sample.duration = 100;
        sample.playing = false;
        check(QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection,
                                        Q_ARG(MediaSample, sample)),
              "Inject media snapshot");
        check(controller.position() == 42 && !controller.playing(), "SMTC paused position");
        sample.position = 10;
        sample.playing = true;
        QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
        check(controller.position() == 10 && controller.playing(), "SMTC backwards seek and resume");
        sample.identity = "two";
        sample.position = 0;
        QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
        check(controller.position() == 0, "Media change resets timeline");
        overlay.resize(800, 600);
        renderer.setParentItem(overlay.contentItem());
        renderer.setSize(QSizeF(800, 600));
        int swaps = 0;
        const auto frameConnection = QObject::connect(&overlay, &QQuickWindow::frameSwapped,
            &controller, [&] { ++swaps; }, Qt::QueuedConnection);
        overlay.show();
        renderer.update();
        // Exclude cold window/device creation from the bounded cadence check.
        wait(300);
        renderer.setSize(QSizeF(overlay.width(), overlay.height()));
        sample.identity = "frame-cadence";
        sample.position = 0;
        QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
        swaps = 0;
        wait(600);
        if (swaps < 8 || controller.position() <= 0.3)
            std::cerr << "sync frame probe: swaps=" << swaps << " position=" << controller.position()
                      << " playing=" << controller.playing() << " exposed=" << overlay.isExposed() << '\n';
        check(swaps >= 8 && controller.position() > 0.3,
              "One playing media snapshot sustains frames without more samples");
        sample.playing = false;
        sample.position = controller.position();
        QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
        wait(100);
        const int pausedSwaps = swaps;
        wait(150);
        check(swaps <= pausedSwaps + 1, "Paused sync stops requesting continuous frames");
        const auto renderId = renderer.firstSnapshotId();
        const auto renderX = renderer.firstSnapshotX();
        const auto oldImage = overlay.grabWindow();
        liveSettings->setValue("fontSize", 48);
        liveSettings->setValue("strokeWidth", 3);
        wait(100);
        const auto newImage = overlay.grabWindow();
        check(renderId != 0 && renderer.firstSnapshotId() == renderId && renderer.firstSnapshotX() == renderX &&
                  renderer.firstSnapshotFontSize() == 48 && !newImage.isNull() && newImage != oldImage,
              "Paused visible text changes pixels without changing identity or movement");
        if (renderer.imageBackend())
            check(renderer.imageNodeCount() == renderer.snapshotCount() && renderer.imageNodeCount() > 0,
                  "Cached raster backend refreshes live resources after style edits");
        const auto artifactDir = qEnvironmentVariable("DANMAKU_SETTINGS_TEST_ARTIFACTS");
        if (!artifactDir.isEmpty()) {
            QDir().mkpath(artifactDir);
            check(oldImage.save(QDir(artifactDir).filePath("before.png")) &&
                      newImage.save(QDir(artifactDir).filePath("after.png")), "Save settings rendering evidence");
        }
        const auto editedSwaps = swaps;
        wait(150);
        check(swaps <= editedSwaps + 1, "Appearance refresh does not restart paused continuous frames");
        sample.playing = true;
        QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
        wait(300);
        check(swaps >= pausedSwaps + 5, "One resume sample restarts continuous frames");
        const auto nativeHideId = renderer.firstSnapshotId();
        const auto nativeHideX = renderer.firstSnapshotX();
        const auto nativeHidePosition = controller.position();
        overlay.hide();
        wait(200);
        const auto hiddenSwaps = swaps;
        wait(120);
        check(controller.position() > nativeHidePosition + 0.2 && swaps <= hiddenSwaps + 1,
              "Native hiding maintains the media clock without continuously rendering");
        overlay.show();
        wait(100);
        check(nativeHideId != 0 && renderer.firstSnapshotId() == nativeHideId &&
                  renderer.firstSnapshotX() < nativeHideX - 50,
              "Native reappearance retains the live ID and advances position instead of restarting");
        sample.playing = false;
        sample.position = controller.position();
        QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
        wait(80);
        const auto hiddenPauseId = renderer.firstSnapshotId();
        const auto hiddenPauseX = renderer.firstSnapshotX();
        overlay.hide();
        wait(100);
        overlay.show();
        wait(100);
        check(renderer.firstSnapshotId() == hiddenPauseId && renderer.firstSnapshotX() == hiddenPauseX &&
                  !controller.playing(), "Paused native hide/show retains the frozen comment and refreshes it");
        QObject::disconnect(frameConnection);
        overlay.hide();
        sample.found = false;
        QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
        check(!controller.playing(), "Missing session freezes playback");
        const auto invalidationsBeforeStop = sceneInvalidations.load();
        controller.stop();
        check(!controller.running(), "Controller stop");
        check(controller.total() == 0 && controller.position() == 0 && controller.duration() == 0 &&
                  !controller.playing() && !controller.manualMode() && !controller.overlayVisible() &&
                  renderer.cacheEntries() == 0 && renderer.snapshotCount() == 0 && !overlay.isVisible(),
              "Stop unloads file, cached layouts and timeline while keeping the file path");
        check(controller.filePath() == path, "Stop preserves the path for explicit reload");
        wait(100);
        if (qEnvironmentVariable("QSG_RENDER_LOOP") == "threaded")
            check(sceneInvalidations.load() > invalidationsBeforeStop,
                  "Stop invalidates the persistent hidden window on the rendering thread");
        check(renderer.sceneEntries() == 0 && renderer.imageNodeCount() == 0 && renderer.textureBytes() == 0,
              "Stop releases the already hidden scene graph and textures");
        controller.stop();
        liveSettings->setValue("maxActive", 1000);
        wait(50);
        controller.start(true);
        check(!controller.running() && controller.total() == 0, "A stopped file requires a reload");
        controller.clearError();
        success = false;
        controller.loadFile(path);
        loop.exec();
        check(success && controller.total() == 2, "Reload after stop succeeds");
        controller.start(true);
        overlay.show();
        wait(100);
        check(controller.running() && renderer.snapshotCount() > 0 && renderer.sceneEntries() > 0 &&
                  overlay.isPersistentSceneGraph() && overlay.isPersistentGraphics(),
              "Playback after reload recreates resources and restores temporary-hide persistence");
        controller.stop();
        wait(50);
        check(renderer.sceneEntries() == 0 && renderer.textureBytes() == 0,
              "Stop releases resources while the overlay is visible");
        const auto completedLoads = loads;
        controller.loadFile(path);
        controller.stop();
        wait(100);
        check(!controller.loading() && controller.total() == 0 && loads == completedLoads,
              "Stop during XML loading cancels and rejects stale completion");
        controller.loadFile(path);
        // Let the worker finish without dispatching the GUI completion callback.
        Sleep(100);
        controller.stop();
        wait(100);
        check(!controller.loading() && controller.total() == 0 && loads == completedLoads,
              "Stop also discards a completed XML payload waiting in the GUI queue");
        success = false;
        controller.loadFile(path);
        loop.exec();
        check(success && controller.total() == 2, "Loading still works after repeated cancellation");
        controller.stop();
    }
    {
        // Deterministic foreground results; never move focus or operate a real player.
        bool foreground = true;
        AppController controller(dir.filePath("hidden-data"), nullptr,
                                 [&](const QString&) { return foreground; });
        QObject::disconnect(controller.findChild<MediaMonitor*>(), nullptr, &controller, nullptr);
        const auto wait = [](int ms) {
            QEventLoop loop;
            QTimer::singleShot(ms, &loop, &QEventLoop::quit);
            loop.exec();
        };
        const auto hiddenPath = write("hidden.xml",
            R"(<i><d p="0,1,25,16777215">keep scrolling</d><d p="0,5,25,16777215">keep fixed</d><d p="0.7,1,25,16777215">suppress while hidden</d></i>)");
        QEventLoop loaded;
        bool success = false;
        QObject::connect(&controller, &AppController::loadCompleted, &loaded, [&](bool ok) { success = ok; loaded.quit(); });
        QTimer::singleShot(10000, &loaded, &QEventLoop::quit);
        controller.loadFile(hiddenPath);
        loaded.exec();
        check(success, "Load foreground visibility fixture");
        auto* settings = qobject_cast<SettingsStore*>(controller.settings());
        settings->setValue("speed", 100);
        settings->setValue("foregroundOnly", true);
        controller.selectSession("hidden-session");
        QQuickWindow overlay;
        DanmakuItem renderer;
        renderer.setParentItem(overlay.contentItem());
        controller.attach(&renderer, &overlay);
        QObject::connect(&controller, &AppController::stateChanged, &overlay, [&] {
            overlay.setVisible(controller.overlayVisible());
        });
        controller.start(false);
        wait(200);
        renderer.setSize(QSizeF(overlay.width(), overlay.height()));
        MediaSample sample;
        sample.found = true; sample.id = "hidden-session"; sample.identity = "visibility";
        sample.position = 0; sample.duration = 100; sample.playing = true;
        const auto inject = [&] {
            QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
        };
        inject();
        wait(100);
        const auto id = renderer.firstSnapshotId();
        const auto x = renderer.firstSnapshotX();
        const auto visibilityArtifacts = qEnvironmentVariable("DANMAKU_VISIBILITY_TEST_ARTIFACTS");
        if (!visibilityArtifacts.isEmpty()) {
            QDir().mkpath(visibilityArtifacts);
            check(overlay.grabWindow().save(QDir(visibilityArtifacts).filePath("before-hide.png")),
                  "Save visibility evidence before hiding");
        }
        foreground = false;
        wait(350);
        check(!controller.overlayVisible() && !overlay.isVisible(), "Foreground loss hides the actual overlay");
        const auto misses = renderer.cacheMisses();
        wait(450);
        check(renderer.cacheMisses() == misses, "Hidden foreground maintenance does not prepare new text resources");
        foreground = true;
        wait(350);
        check(controller.overlayVisible() && id != 0 && renderer.firstSnapshotId() == id &&
                  renderer.firstSnapshotX() < x - 70 && renderer.snapshotCount() == 2,
              "Foreground restoration keeps scroll/fixed objects and does not emit hidden comments");
        if (!visibilityArtifacts.isEmpty())
            check(overlay.grabWindow().save(QDir(visibilityArtifacts).filePath("after-show.png")),
                  "Save visibility evidence after restoring");
        sample.position = controller.position(); sample.playing = false;
        inject();
        wait(80);
        const auto pausedId = renderer.firstSnapshotId();
        const auto pausedX = renderer.firstSnapshotX();
        foreground = false;
        wait(350);
        const auto frozenPosition = controller.position();
        settings->setValue("fontSize", 36);
        wait(80);
        foreground = true;
        wait(350);
        check(controller.overlayVisible() && renderer.firstSnapshotId() == pausedId &&
                  renderer.firstSnapshotX() == pausedX && renderer.firstSnapshotFontSize() == 36 &&
                  controller.position() == frozenPosition && renderer.snapshotCount() == 2,
              "Paused foreground hide/show and hidden settings edit preserve identity, position and lifetime");
        foreground = false;
        wait(350);
        sample.position = 20; sample.playing = true;
        inject();
        wait(80);
        foreground = true;
        wait(350);
        check(renderer.snapshotCount() == 0, "A real seek while hidden still clears stale active objects");
        controller.stop();
        overlay.hide();
    }
    if (argc > 1) {
        auto sample = readDanmakuXml(QString::fromLocal8Bit(argv[1]));
        check(sample.error.isEmpty() && sample.items.size() == 3600, "Repository XML compatibility");
    }
    return failed ? 1 : 0;
}
