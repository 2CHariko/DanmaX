#pragma once
#include "core/DanmakuEngine.h"
#include <QString>
#include <functional>
#include <stop_token>
struct XmlResult {
    std::vector<danmaku::Item> items;
    QString error;
    int skipped{};
    int invalidRecords{};
    int unsupportedModes{};
    int sanitizedCharacters{};
    int defaultedColors{};
    bool cancelled{};
};
XmlResult readDanmakuXml(const QString& path, std::stop_token stop = {},
                         const std::function<void(int)>& progress = {});
