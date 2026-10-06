#pragma once
#include <QString>

struct AppPaths {
    QString data;
    QString cache;
    static AppPaths prepare(const QString& executable, const QString& dataOverride,
                            const QString& cacheOverride);
};
