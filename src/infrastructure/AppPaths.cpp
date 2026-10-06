#include "infrastructure/AppPaths.h"
#include <QDir>
#include <QFileInfo>
#include <QTemporaryFile>
#include <stdexcept>

AppPaths AppPaths::prepare(const QString& executable, const QString& dataOverride,
                          const QString& cacheOverride) {
    const QDir executableDir(QFileInfo(executable).absolutePath());
    AppPaths paths{
        QDir::cleanPath(dataOverride.isEmpty() ? executableDir.filePath("data") : QFileInfo(dataOverride).absoluteFilePath()),
        QDir::cleanPath(cacheOverride.isEmpty() ? executableDir.filePath("data/cache") : QFileInfo(cacheOverride).absoluteFilePath())
    };
    for (const auto& path : {paths.data, paths.cache}) {
        if (!QDir().mkpath(path)) throw std::runtime_error("Cannot create application data/cache directory");
        QTemporaryFile probe(QDir(path).filePath("write-check-XXXXXX"));
        if (!probe.open()) throw std::runtime_error("Application data/cache directory is not writable");
    }
    return paths;
}
