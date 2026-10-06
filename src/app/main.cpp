#include "application/AppController.h"
#include "infrastructure/AppPaths.h"
#include <QCommandLineParser>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickGraphicsConfiguration>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QTimer>
#include <cstdio>
#include <exception>

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    app.setApplicationName("LocalDanmaku");
    app.setApplicationVersion("0.1.0");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data-dir", "Writable application data directory", "path"});
    parser.addOption({"cache-dir", "Writable QML cache directory", "path"});
    parser.addOption({"smoke-test", "Load both windows and exit automatically"});
    parser.addOption({"capture", "Save the control window during smoke testing", "path"});
    parser.process(app);
    try {
        const auto paths = AppPaths::prepare(app.applicationFilePath(), parser.value("data-dir"), parser.value("cache-dir"));
        qputenv("QML_DISK_CACHE_PATH", paths.cache.toUtf8());
        AppController controller(paths.data);
        QQmlApplicationEngine engine;
        bool creationFailed = false;
        bool qmlWarning = false;
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app, [&] { qmlWarning = true; });
        QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [&] {
            creationFailed = true;
            app.exit(1);
        });
        engine.setInitialProperties({{"backend", QVariant::fromValue(&controller)}});
        engine.loadFromModule("LocalDanmaku", "Main");
        if (creationFailed || engine.rootObjects().isEmpty()) return 1;
        int windowIndex = 0;
        for (auto* window : app.allWindows()) {
            if (auto* quickWindow = qobject_cast<QQuickWindow*>(window)) {
                QQuickGraphicsConfiguration graphics;
                graphics.setAutomaticPipelineCache(false);
                const QString pipelineCache = QDir(paths.cache).filePath(
                    QStringLiteral("pipeline-%1.bin").arg(windowIndex++));
                graphics.setPipelineCacheSaveFile(pipelineCache);
                if (QFileInfo::exists(pipelineCache)) graphics.setPipelineCacheLoadFile(pipelineCache);
                quickWindow->setGraphicsConfiguration(graphics);
            }
        }
        auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!mainWindow) return 1;
        mainWindow->show();
        if (parser.isSet("smoke-test")) {
            controller.setOverlayVisible(true);
            QTimer::singleShot(700, &app, [&] {
                auto* preview = qobject_cast<PreviewDriver*>(controller.preview());
                bool passed = preview && preview->elapsedSeconds() > 0 && !qmlWarning;
                passed = passed && app.allWindows().size() >= 2;
                if (parser.isSet("capture")) {
                    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
                    passed = window && window->grabWindow().save(parser.value("capture")) && passed;
                }
                controller.setOverlayVisible(false);
                app.exit(passed ? 0 : 2);
            });
        }
        return app.exec();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s\n", error.what());
        return 1;
    }
}
