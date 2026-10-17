
#include "application/AppController.h"
#include "platform/windows/WindowBackdropController.h"
#include "infrastructure/AppPaths.h"
#include <QWKQuick/qwkquickglobal.h>
#include "version.h"
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QImage>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickGraphicsConfiguration>
#include <QQuickWindow>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <memory>
#include <stdexcept>
#include <windows.h>
#include <shellapi.h>

namespace {
bool preparePortableCache(const QString& cache) {
    const auto temporary = QDir(cache).filePath("tmp");
    return QDir().mkpath(temporary) &&
           _wputenv_s(L"TEMP", reinterpret_cast<const wchar_t*>(temporary.utf16())) == 0 &&
           _wputenv_s(L"TMP", reinterpret_cast<const wchar_t*>(temporary.utf16())) == 0 &&
           _wputenv_s(L"QML_DISK_CACHE_PATH", reinterpret_cast<const wchar_t*>(cache.utf16())) == 0;
}
} // namespace

int main(int argc, char* argv[]) {
    // Set process-only cache/temp paths before QGuiApplication can initialize plugins.
    wchar_t executablePath[32768]{};
    const DWORD executableLength = GetModuleFileNameW(nullptr, executablePath, 32768);
    if (!executableLength || executableLength >= 32768)
        return 1;
    const QDir executableDirectory(QFileInfo(QString::fromWCharArray(executablePath)).absolutePath());
    QString earlyData = executableDirectory.absolutePath(), earlyCache;
    const auto resolveData = [&](const QString& value) {
        return value.isEmpty() ? executableDirectory.absolutePath() : QFileInfo(value).absoluteFilePath();
    };
    const auto resolveCache = [](const QString& value) {
        return value.isEmpty() ? QString() : QFileInfo(value).absoluteFilePath();
    };
    int argumentCount = 0;
    auto** wideArguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!wideArguments)
        return 1;
    for (int index = 1; index < argumentCount; ++index) {
        const QString argument = QString::fromWCharArray(wideArguments[index]);
        if (argument == "--")
            break;
        if (argument == "--data-dir" && index + 1 < argumentCount)
            earlyData = resolveData(QString::fromWCharArray(wideArguments[++index]));
        else if (argument.startsWith("--data-dir="))
            earlyData = resolveData(argument.mid(11));
        else if (argument == "--cache-dir" && index + 1 < argumentCount)
            earlyCache = resolveCache(QString::fromWCharArray(wideArguments[++index]));
        else if (argument.startsWith("--cache-dir="))
            earlyCache = resolveCache(argument.mid(12));
    }
    LocalFree(wideArguments);
    if (earlyCache.isEmpty())
        earlyCache = QDir(earlyData).filePath("cache");
    if (!preparePortableCache(earlyCache)) {
        std::fprintf(stderr, "Cannot prepare portable cache/temporary directory\n");
        return 1;
    }
#ifdef DANMAKU_STATIC_PORTABLE
    // Select the public D3D11 backend before Qt creates any Quick windows.
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    qputenv("QT_PLUGIN_PATH", QByteArray());
    qputenv("QT_QPA_PLATFORM_PLUGIN_PATH", QByteArray());
    qputenv("QML_IMPORT_PATH", QByteArray());
    qputenv("QML2_IMPORT_PATH", QByteArray());
#endif
    QGuiApplication app(argc, argv);

    QFont uiFont = app.font();
    uiFont.setFamilies({"Segoe UI", "Microsoft YaHei UI"});
    app.setFont(uiFont);
    if (QFontDatabase::families().contains("Microsoft YaHei UI"))
        QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Han, "Microsoft YaHei UI");
    app.setApplicationName("DanmaX");
    app.setApplicationDisplayName("DanmaX");
    app.setWindowIcon(QIcon(":/branding/DanmaX.png"));
    app.setApplicationVersion(QString::fromUtf8(DANMAX_DISPLAY_VERSION));
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data-dir", "Writable application data directory", "path"});
    parser.addOption({"cache-dir", "Writable application cache directory", "path"});
    parser.addOption({"smoke-test", "Load both windows and exit automatically"});
    parser.addOption({"capture", "Save the control window during smoke testing", "path"});
    parser.addOption({"capture-overlay", "Save the overlay during validation", "path"});
    parser.addOption({"benchmark", "Generate a synthetic workload with this active count", "count"});
    parser.addOption({"seconds", "Benchmark duration", "seconds", "15"});
    parser.addOption({"report", "Write benchmark metrics to JSON", "path"});
    parser.addOption({"file", "Load an XML file on startup", "path"});
    parser.addOption({"page", "Initial page: home, player, logs, settings, about", "page", "home"});
    parser.addOption({"online-search", "Open online danmaku search on the home page (UI validation)"});
    parser.addOption({"theme", "Theme for isolated UI validation", "theme"});
    parser.addOption({"window-width", "Width for layout validation", "pixels"});
    parser.addOption({"capture-state", "UI smoke capture state: settings-middle, font-popup, diagnostics-group, cache-group or metadata-cache-group", "state"});
    parser.process(app);
    try {
        const QStringList pageNames{"home", "player", "logs", "settings", "about"};
        const int initialPage = pageNames.indexOf(parser.value("page"));
        if (initialPage < 0) throw std::runtime_error("Unknown initial page");
        const auto paths =
            AppPaths::prepare(app.applicationFilePath(), parser.value("data-dir"), parser.value("cache-dir"));
        qmlRegisterType<DanmakuItem>("DanmaX.Native", 1, 0, "DanmakuCanvas");
        AppController controller(paths.data, nullptr, {}, paths.cache);
        if (parser.isSet("theme"))
            qobject_cast<SettingsStore*>(controller.settings())->setValue("theme", parser.value("theme"));
        WindowBackdropController backdrop(qobject_cast<SettingsStore*>(controller.settings()), qobject_cast<LogModel*>(controller.logs()));
        QQmlApplicationEngine engine;
        bool creationFailed = false;
        bool qmlWarning = false;
        QObject::connect(
            &engine, &QQmlApplicationEngine::warnings, &app, [&](const QList<QQmlError>& errors) {
                qmlWarning = true;
                for (const auto& error : errors) {
                    qobject_cast<LogModel*>(controller.logs())->append("ERROR", error.toString());
                    std::fprintf(stderr, "%s\n", error.toString().toUtf8().constData());
                }
            });
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [&] {
            creationFailed = true;
            app.exit(1);
        });
        engine.setInitialProperties({{"backdrop", QVariant::fromValue(&backdrop)}, {"backend", QVariant::fromValue(&controller)},
                                     {"selectedPage", initialPage}});
        QWK::registerTypes(&engine);
        engine.loadFromModule("DanmaX", "Main");
        if (creationFailed || engine.rootObjects().isEmpty())
            return 1;
        if (parser.isSet("online-search")) controller.showOnlineDanmakuForAnime({});
        int windowIndex = 0;
        for (auto* window : app.allWindows()) {
            if (auto* quickWindow = qobject_cast<QQuickWindow*>(window)) {
                QQuickGraphicsConfiguration graphics;
                graphics.setAutomaticPipelineCache(false);
                const QString pipelineCache =
                    QDir(paths.cache).filePath(QStringLiteral("pipeline-%1.bin").arg(windowIndex++));
                graphics.setPipelineCacheSaveFile(pipelineCache);
                if (QFileInfo::exists(pipelineCache))
                    graphics.setPipelineCacheLoadFile(pipelineCache);
                quickWindow->setGraphicsConfiguration(graphics);
            }
        }
        auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!mainWindow)
            return 1;
        if (parser.isSet("window-width"))
            mainWindow->setWidth(parser.value("window-width").toInt());
        backdrop.attach(mainWindow);
        bool overlayCaptured = false;
        mainWindow->show();
        // Validation workloads must satisfy the same foreground policy as normal playback.
        // UI captures restore the panel after the overlay has actually presented a frame.
        if (parser.isSet("smoke-test") || parser.isSet("benchmark")) {
            mainWindow->showMinimized();
            if (parser.isSet("capture")) {
                auto restored = std::make_shared<bool>(false);
                for (auto* window : app.allWindows()) {
                    if (window->objectName() != "danmakuOverlay") continue;
                    auto* quick = qobject_cast<QQuickWindow*>(window);
                    if (!quick) continue;
                    QObject::connect(quick, &QQuickWindow::frameSwapped, mainWindow,
                                     [&, mainWindow, quick, restored] {
                        if (*restored) return;
                        *restored = true;
                        QTimer::singleShot(200, mainWindow, [&, mainWindow, quick] {
                            // Combined captures save the overlay before activating its panel.
                            if (parser.isSet("capture-overlay"))
                                overlayCaptured = quick->isVisible() && quick->isExposed() &&
                                    quick->grabWindow().save(parser.value("capture-overlay"));
                            mainWindow->showNormal();
                            mainWindow->requestActivate();
                        });
                    }, Qt::QueuedConnection);
                }
            }
        }
        if (parser.isSet("smoke-test") && parser.isSet("capture-state")) {
            QTimer::singleShot(300, &app, [&, mainWindow] {
                mainWindow->setProperty("selectedPage", pageNames.indexOf("settings"));
                QTimer::singleShot(100, &app, [&, mainWindow] {
                    auto* page = mainWindow->findChild<QObject*>("settingsPage");
                    if (!page) { qmlWarning = true; return; }
                    if (parser.value("capture-state") == "settings-middle") {
                        auto* bar = page->property("verticalBar").value<QObject*>();
                        if (bar) bar->setProperty("position", 0.4);
                        else qmlWarning = true;
                    } else if (parser.value("capture-state") == "diagnostics-group" || parser.value("capture-state") == "cache-group" || parser.value("capture-state") == "metadata-cache-group") {
                        const auto name = parser.value("capture-state") == "diagnostics-group" ? "diagnosticsGroup"
                            : parser.value("capture-state") == "metadata-cache-group" ? "metadataCacheGroup" : "cacheGroup";
                        auto* group = mainWindow->findChild<QQuickItem*>(name);
                        if (!group) { qmlWarning = true; return; }
                        QTimer::singleShot(100, page, [page, group] {
                            QMetaObject::invokeMethod(page, "reveal", Q_ARG(QVariant, QVariant::fromValue(group)));
                            group->setFocus(false);
                        });
                    } else if (parser.value("capture-state") == "font-popup") {
                        auto* font = mainWindow->findChild<QQuickItem*>("fontFamilyCombo");
                        if (!font) { qmlWarning = true; return; }
                        QMetaObject::invokeMethod(page, "reveal", Q_ARG(QVariant, QVariant::fromValue(font)));
                        auto* popup = font->property("popup").value<QObject*>();
                        if (popup) QMetaObject::invokeMethod(popup, "open");
                        else qmlWarning = true;
                    } else qmlWarning = true;
                });
            });
        }
        if (parser.isSet("file") && !parser.isSet("smoke-test") && !parser.isSet("benchmark"))
            throw std::runtime_error("--file is only available for internal smoke/benchmark validation; open video in the application");
        if (parser.isSet("file")) {
            QObject::connect(&controller, &AppController::loadCompleted, &app, [&](bool success) {
                if (success)
                    controller.start(true);
            });
            controller.loadFile(parser.value("file"));
        }
        if (parser.isSet("smoke-test") || parser.isSet("benchmark")) {
            const int count =
                parser.isSet("benchmark") ? std::clamp(parser.value("benchmark").toInt(), 50, 5000) : 40;
            if (!parser.isSet("file"))
                controller.beginDemo(count);
            const int duration =
                parser.isSet("benchmark") ? std::clamp(parser.value("seconds").toInt(), 1, 300) * 1000
                    : parser.isSet("seconds") ? std::clamp(parser.value("seconds").toInt(), 1, 30) * 1000 : 1500;
            QTimer::singleShot(duration, &app, [&] {
                bool passed = controller.position() > 0 && !qmlWarning && app.allWindows().size() >= 2;
                if (parser.isSet("smoke-test"))
                    passed = passed && mainWindow->property("currentPageName").toString() ==
                        (parser.isSet("capture-state") ? QStringLiteral("settings") : parser.value("page"));
                if (parser.isSet("smoke-test") && !parser.isSet("file") &&
                    qEnvironmentVariable("DANMAKU_RENDER_BACKEND") != "text")
                    passed = passed && controller.metrics().value("imageNodes").toInt() > 0;
                if (parser.isSet("capture"))
                    passed = mainWindow->grabWindow().save(parser.value("capture")) && passed;
                if (parser.isSet("capture-overlay") && !overlayCaptured)
                    for (auto* window : app.allWindows())
                        if (window->objectName() == "danmakuOverlay")
                            if (auto* quick = qobject_cast<QQuickWindow*>(window))
                                passed = quick->isVisible() && quick->isExposed() &&
                                         quick->grabWindow().save(parser.value("capture-overlay")) && passed;
                if (parser.isSet("report"))
                    controller.writeReport(parser.value("report"));
                if (!passed)
                    std::fprintf(stderr, "Smoke failed: position=%.3f qmlWarning=%d windows=%lld imageNodes=%d active=%d suppressedWhileHidden=%llu maintenanceTicks=%llu\n",
                                 controller.position(), qmlWarning, static_cast<long long>(app.allWindows().size()),
                                 controller.metrics().value("imageNodes").toInt(),
                                 controller.metrics().value("active").toInt(),
                                 controller.metrics().value("suppressedWhileHidden").toULongLong(),
                                 controller.metrics().value("maintenanceTicks").toULongLong());
                controller.unload();
                app.exit(passed ? 0 : 2);
            });
        }
        return app.exec();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
