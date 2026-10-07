#include "application/AppController.h"
#include "infrastructure/AppPaths.h"
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickGraphicsConfiguration>
#include <QQuickWindow>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <exception>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QFont uiFont = app.font();
    uiFont.setFamilies({"Segoe UI", "Microsoft YaHei UI"});
    app.setFont(uiFont);
    if (QFontDatabase::families().contains("Microsoft YaHei UI"))
        QFontDatabase::addApplicationFallbackFontFamily(QChar::Script_Han, "Microsoft YaHei UI");
    app.setApplicationName("LocalDanmaku");
    app.setApplicationVersion("0.2.0");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data-dir", "Writable application data directory", "path"});
    parser.addOption({"cache-dir", "Writable QML cache directory", "path"});
    parser.addOption({"smoke-test", "Load both windows and exit automatically"});
    parser.addOption({"capture", "Save the control window during smoke testing", "path"});
    parser.addOption({"capture-overlay", "Save the overlay during validation", "path"});
    parser.addOption({"benchmark", "Generate a synthetic workload with this active count", "count"});
    parser.addOption({"seconds", "Benchmark duration", "seconds", "15"});
    parser.addOption({"report", "Write benchmark metrics to JSON", "path"});
    parser.addOption({"media-session", "Select a real SMTC session (read only)", "id"});
    parser.addOption({"file", "Load an XML file on startup", "path"});
    parser.addOption({"page", "Initial page: player, logs, settings", "page", "player"});
    parser.addOption({"theme", "Theme for isolated UI validation", "theme"});
    parser.addOption({"window-width", "Width for layout validation", "pixels"});
    parser.process(app);
    try {
        const auto paths =
            AppPaths::prepare(app.applicationFilePath(), parser.value("data-dir"), parser.value("cache-dir"));
        qputenv("QML_DISK_CACHE_PATH", paths.cache.toUtf8());
        qmlRegisterType<DanmakuItem>("LocalDanmaku.Native", 1, 0, "DanmakuCanvas");
        AppController controller(paths.data);
        if (parser.isSet("theme"))
            qobject_cast<SettingsStore*>(controller.settings())->setValue("theme", parser.value("theme"));
        if (parser.isSet("media-session"))
            controller.selectSession(parser.value("media-session"));
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
        engine.setInitialProperties({{"backend", QVariant::fromValue(&controller)},
                                     {"selectedPage", parser.value("page") == "settings" ? 2
                                                      : parser.value("page") == "logs"   ? 1
                                                                                         : 0}});
        engine.loadFromModule("LocalDanmaku", "Main");
        if (creationFailed || engine.rootObjects().isEmpty())
            return 1;
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
        mainWindow->show();
        if (parser.isSet("file")) {
            QObject::connect(&controller, &AppController::loadCompleted, &app, [&](bool success) {
                if (success)
                    controller.start(!parser.isSet("media-session"));
            });
            controller.loadFile(parser.value("file"));
        }
        if (parser.isSet("smoke-test") || parser.isSet("benchmark")) {
            const int count =
                parser.isSet("benchmark") ? std::clamp(parser.value("benchmark").toInt(), 50, 5000) : 40;
            if (!parser.isSet("file"))
                controller.beginDemo(count);
            const int duration =
                parser.isSet("benchmark") ? std::clamp(parser.value("seconds").toInt(), 1, 300) * 1000 : 1500;
            QTimer::singleShot(duration, &app, [&] {
                bool passed = controller.position() > 0 && !qmlWarning && app.allWindows().size() >= 2;
                if (parser.isSet("smoke-test") && !parser.isSet("file") &&
                    qEnvironmentVariable("DANMAKU_RENDER_BACKEND") == "image")
                    passed = passed && controller.metrics().value("imageNodes").toInt() > 0;
                if (parser.isSet("media-session"))
                    passed = passed && !controller.manualMode() && !controller.mediaTitle().isEmpty();
                if (parser.isSet("capture"))
                    passed = mainWindow->grabWindow().save(parser.value("capture")) && passed;
                if (parser.isSet("capture-overlay"))
                    for (auto* window : app.allWindows())
                        if (window != mainWindow)
                            if (auto* quick = qobject_cast<QQuickWindow*>(window))
                                passed = quick->grabWindow().save(parser.value("capture-overlay")) && passed;
                if (parser.isSet("report"))
                    controller.writeReport(parser.value("report"));
                controller.stop();
                app.exit(passed ? 0 : 2);
            });
        }
        return app.exec();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
