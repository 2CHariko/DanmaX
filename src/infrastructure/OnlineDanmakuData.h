#pragma once
#include "core/DanmakuEngine.h"
#include <QByteArray>
#include <QVariantList>
#include <QVariantMap>
#include <QString>
#include <stop_token>

struct OnlineDanmakuResult {
    std::vector<danmaku::Item> items;
    QVariantMap metadata;
    QString error;
    QString warning;
    int invalid{};
    int unsupported{};
    int duplicates{};
    bool cancelled{};
};

QString normalizeDanmakuServer(const QString& address);
QString danmakuCacheKey(const QString& server, qint64 episodeId);
OnlineDanmakuResult parseOnlineDanmaku(const QByteArray& json, std::stop_token stop = {});
OnlineDanmakuResult readCachedDanmaku(const QString& directory, const QString& key,
                                    std::stop_token stop = {});
QString saveCachedDanmaku(const QString& directory, const OnlineDanmakuResult& result,
                         std::stop_token stop = {});
QVariantList listCachedDanmaku(const QString& directory, std::stop_token stop = {});
QString deleteCachedDanmaku(const QString& directory, const QString& key);
