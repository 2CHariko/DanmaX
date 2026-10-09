#include "infrastructure/AppPaths.h"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <stdexcept>

AppPaths AppPaths::prepare(const QString& executable, const QString& dataOverride,
                           const QString& cacheOverride) {
    const QDir executableDir(QFileInfo(executable).absolutePath());
#ifdef DANMAKU_STATIC_PORTABLE
    const QString defaultData = executableDir.absolutePath();
#else
    const QString defaultData = executableDir.filePath("data");
#endif
    const QString data = QDir::cleanPath(dataOverride.isEmpty() ? defaultData
                                                             : QFileInfo(dataOverride).absoluteFilePath());
#ifdef DANMAKU_STATIC_PORTABLE
    const QString defaultCache = QDir(data).filePath("cache");
#else
    const QString defaultCache = executableDir.filePath("data/cache");
#endif
    AppPaths paths{data,
                   QDir::cleanPath(cacheOverride.isEmpty() ? defaultCache
                                                           : QFileInfo(cacheOverride).absoluteFilePath())};
    for (const auto& path : {paths.data, paths.cache}) {
        if (!QDir().mkpath(path))
            throw std::runtime_error("Cannot create application data/cache directory");
        QTemporaryFile probe(QDir(path).filePath("write-check-XXXXXX"));
        if (!probe.open())
            throw std::runtime_error("Application data/cache directory is not writable");
    }
    return paths;
}
