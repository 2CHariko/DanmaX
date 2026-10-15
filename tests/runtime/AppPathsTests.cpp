#include "infrastructure/AppPaths.h"
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <stdexcept>

class AppPathsTests final : public QObject {
    Q_OBJECT
  private slots:
    void defaultPortableLayout() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto directory = root.filePath(QStringLiteral("中文 portable"));
        const auto paths = AppPaths::prepare(QDir(directory).filePath("DanmaX.exe"), {}, {});
        QCOMPARE(paths.data, directory);
        QCOMPARE(paths.cache, QDir(directory).filePath("cache"));
        QVERIFY(QDir(paths.cache).exists());
    }
    void dataOverrideOwnsDefaultCache() {
        QTemporaryDir root;
        const auto data = root.filePath("separate data");
        const auto paths = AppPaths::prepare(root.filePath("runtime/DanmaX.exe"), data, {});
        QCOMPARE(paths.data, data);
        QCOMPARE(paths.cache, QDir(data).filePath("cache"));
        QVERIFY(!QDir(root.filePath("runtime/cache")).exists());
    }
    void relativeOverridesUseCallerDirectory() {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        const auto before = QDir::currentPath();
        const auto restore = qScopeGuard([before] { QDir::setCurrent(before); });
        QVERIFY(QDir::setCurrent(root.path()));
        const auto paths = AppPaths::prepare(root.filePath("runtime/DanmaX.exe"), "data", "separate cache");
        QCOMPARE(paths.data, root.filePath("data"));
        QCOMPARE(paths.cache, root.filePath("separate cache"));
    }
    void buildsDoNotShareDefaults() {
        QTemporaryDir root;
        const auto debug = AppPaths::prepare(root.filePath("windows-debug/DanmaX.exe"), {}, {});
        const auto release = AppPaths::prepare(root.filePath("windows-release/DanmaX.exe"), {}, {});
        QVERIFY(debug.data != release.data);
        QVERIFY(debug.cache != release.cache);
    }
    void blockedPathsFailWithoutFallback() {
        QTemporaryDir root;
        QFile blocked(root.filePath("blocked"));
        QVERIFY(blocked.open(QIODevice::WriteOnly));
        QCOMPARE(blocked.write("preserve"), qint64(8));
        blocked.close();
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, AppPaths::prepare(root.filePath("DanmaX.exe"), blocked.fileName(), {}));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, AppPaths::prepare(root.filePath("DanmaX.exe"), {}, blocked.fileName()));
        QVERIFY(blocked.open(QIODevice::ReadOnly));
        QCOMPARE(blocked.readAll(), QByteArray("preserve"));
        QVERIFY(!QDir(root.filePath("data")).exists());
    }
};
QTEST_GUILESS_MAIN(AppPathsTests)
#include "AppPathsTests.moc"
