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
              result.items[0].color == 255,
          "XML sorting, entities and color");
    check(!readDanmakuXml(write("bad.xml", "<i><d>")).error.isEmpty(), "Malformed XML error");
    check(!readDanmakuXml(write("dtd.xml", "<!DOCTYPE i [<!ENTITY x 'x'>]><i/>")).error.isEmpty(),
          "Reject DTD");
    check(!readDanmakuXml(dir.filePath("missing.xml")).error.isEmpty(), "Missing file error");
    std::stop_source source;
    source.request_stop();
    check(readDanmakuXml(path, source.get_token()).cancelled, "Cooperative XML cancellation");
    {
        SettingsStore settings(dir.path());
        check(settings.setValue("speed", 300), "Save valid setting");
        check(!settings.setValue("speed", 0) && settings.values()["speed"].toInt() == 300,
              "Reject invalid setting without mutation");
        check(settings.retrySave(), "Flush numeric edits");
        SettingsStore restored(dir.path());
        check(restored.values()["speed"].toInt() == 300, "Restore persisted settings");
        const auto ini = write(
            "legacy.ini", "[Display]\nfont_size=30\n[Danmaku]\nfixed_duration_ms=7000\nallow_overlap=true\n");
        check(restored.importIni(ini) && restored.values()["fontSize"].toInt() == 30 &&
                  restored.values()["fixedSeconds"].toDouble() == 7 && restored.values()["overlap"].toBool(),
              "Legacy INI import");
    }
    write("settings.json", "broken");
    {
        SettingsStore broken(dir.path());
        check(!broken.error().isEmpty(), "Corrupt config reports failure");
        check(broken.setValue("speed", 200), "Recover config");
        check(!QDir(dir.path()).entryList({"settings.json.backup-*"}).isEmpty(), "Corrupt config backup");
    }
    {
        AppController controller(dir.path());
        QEventLoop loop;
        bool success = false;
        QObject::connect(&controller, &AppController::loadCompleted, &loop, [&](bool ok) {
            success = ok;
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
        QQuickWindow overlay;
        DanmakuItem renderer;
        controller.attach(&renderer, &overlay);
        auto appearance = SettingsStore::defaults();
        renderer.configure(appearance);
        danmaku::Item text{0, danmaku::Mode::Scroll, "cache check", 0xffffff};
        const auto firstWidth = renderer.measure(text);
        renderer.measure(text);
        check(renderer.cacheEntries() == 1 && renderer.cacheHits() > 0, "Text layout reuse");
        appearance["fontSize"] = 48;
        renderer.configure(appearance);
        check(renderer.cacheEntries() == 0 && renderer.measure(text) > firstWidth,
              "Font change invalidates layout");
        controller.start(true);
        auto wait = [&](int ms) {
            QEventLoop delay;
            QTimer::singleShot(ms, &delay, &QEventLoop::quit);
            delay.exec();
        };
        wait(80);
        check(controller.position() > 0, "Manual clock advances");
        controller.togglePause();
        const auto paused = controller.position();
        wait(80);
        check(controller.position() == paused, "Manual pause freezes position");
        controller.seek(1);
        check(controller.position() == 1, "Manual seek");
        controller.selectSession("test-session");
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
        sample.found = false;
        QMetaObject::invokeMethod(&controller, "onSample", Qt::DirectConnection, Q_ARG(MediaSample, sample));
        check(!controller.playing(), "Missing session freezes playback");
        controller.stop();
        check(!controller.running(), "Controller stop");
    }
    if (argc > 1) {
        auto sample = readDanmakuXml(QString::fromLocal8Bit(argv[1]));
        check(sample.error.isEmpty() && sample.items.size() == 3600, "Repository XML compatibility");
    }
    return failed ? 1 : 0;
}
