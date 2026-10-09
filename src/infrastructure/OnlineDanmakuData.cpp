#include "infrastructure/OnlineDanmakuData.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace {
constexpr qint64 maxCacheBytes = 128LL * 1024 * 1024;
constexpr qsizetype maxComments = 1000000;
QString cachePath(const QString& directory, const QString& key) {
    static const QRegularExpression digest("^[a-f0-9]{64}$");
    if (!digest.match(key).hasMatch() || QFileInfo(directory).isSymLink()) return {};
    const auto path = QDir(directory).filePath(key + ".json");
    const QFileInfo info(path);
    if (info.isSymLink() || (info.exists() && !info.isFile())) return {};
    return path;
}
void finishItems(OnlineDanmakuResult& result, std::stop_token stop) {
    std::size_t comparisons = 0;
    // Throw only within this worker operation to interrupt a long stable sort.
    try {
        std::stable_sort(result.items.begin(), result.items.end(), [&](const auto& a, const auto& b) {
            if ((++comparisons & 4095) == 0 && stop.stop_requested()) throw 0;
            return a.time < b.time;
        });
    } catch (int) { result.cancelled = true; }
    if (stop.stop_requested() || result.cancelled) {
        result.cancelled = true;
        result.items.clear();
        return;
    }
    QSet<QByteArray> seen;
    std::vector<danmaku::Item> unique;
    unique.reserve(result.items.size());
    for (auto& item : result.items) {
        if (stop.stop_requested()) { result.cancelled = true; result.items.clear(); return; }
        QByteArray key = QByteArray::number(item.time, 'g', 17) + '\0' +
            QByteArray::number(static_cast<int>(item.mode)) + '\0' + QByteArray::number(item.color) + '\0';
        key.append(item.text.data(), static_cast<qsizetype>(item.text.size()));
        if (seen.contains(key)) ++result.duplicates;
        else { seen.insert(key); unique.push_back(std::move(item)); }
    }
    result.items = std::move(unique);
}
bool appendItem(OnlineDanmakuResult& result, double time, bool timeOk, int mode, bool modeOk,
                QString text, qulonglong color, bool colorOk, int fontSize = 25) {
    text = text.trimmed();
    if (!timeOk || !std::isfinite(time) || time < 0 || time > 604800 || !modeOk ||
        !colorOk || color > 0xffffff || text.isEmpty() || text.size() > 512 || fontSize < 1 || fontSize > 200) {
        ++result.invalid;
        return false;
    }
    if (mode < 1 || mode > 5) { ++result.unsupported; return false; }
    for (auto& ch : text) {
        if (ch.unicode() < 32 && ch != QChar('\t')) ch = QChar(' ');
    }
    result.items.push_back({time, mode <= 3 ? danmaku::Mode::Scroll : static_cast<danmaku::Mode>(mode),
                           text.toUtf8().toStdString(), static_cast<std::uint32_t>(color), fontSize});
    return true;
}
QByteArray readLimited(const QString& path, QString& error, std::stop_token stop) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { error = file.errorString(); return {}; }
    if (file.size() > maxCacheBytes) { error = QStringLiteral("缓存超过 128 MiB 限制"); return {}; }
    QByteArray bytes;
    while (!file.atEnd() && !stop.stop_requested()) {
        bytes += file.read(256 * 1024);
        if (file.error() != QFileDevice::NoError) { error = file.errorString(); return {}; }
        if (bytes.size() > maxCacheBytes) { error = QStringLiteral("缓存超过 128 MiB 限制"); return {}; }
    }
    return bytes;
}
} // namespace

QString normalizeDanmakuServer(const QString& address) {
    if (address.trimmed().isEmpty()) return {};
    const QUrl url(address.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment()) return {};
    auto canonical = url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash).toString(QUrl::FullyEncoded);
    if ((url.scheme() == "https" && url.port() == 443) || (url.scheme() == "http" && url.port() == 80)) {
        auto adjusted = QUrl(canonical); adjusted.setPort(-1); canonical = adjusted.toString(QUrl::FullyEncoded);
    }
    return canonical;
}
QString danmakuCacheKey(const QString& server, qint64 episodeId) {
    return QString::fromLatin1(QCryptographicHash::hash(
        (normalizeDanmakuServer(server) + '\n' + QString::number(episodeId) + "\nwithRelated=true&chConvert=0").toUtf8(),
        QCryptographicHash::Sha256).toHex());
}
OnlineDanmakuResult parseOnlineDanmaku(const QByteArray& json, std::stop_token stop) {
    OnlineDanmakuResult result;
    if (json.size() > 64LL * 1024 * 1024) { result.error = QStringLiteral("响应超过 64 MiB 限制"); return result; }
    if (stop.stop_requested()) { result.cancelled = true; return result; }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("弹幕 JSON 格式错误：") + error.errorString(); return result;
    }
    const auto object = document.object();
    if (object.value("success").isBool() && !object.value("success").toBool()) {
        result.error = object.value("errorMessage").toString(QStringLiteral("服务返回失败")); return result;
    }
    if (!object.value("comments").isArray()) { result.error = QStringLiteral("响应缺少 comments 数组"); return result; }
    const auto comments = object.value("comments").toArray();
    if (comments.size() > maxComments) { result.error = QStringLiteral("弹幕条数超过 100 万限制"); return result; }
    result.items.reserve(static_cast<std::size_t>(comments.size()));
    for (const auto& value : comments) {
        if (stop.stop_requested()) { result.cancelled = true; result.items.clear(); return result; }
        if (!value.isObject()) { ++result.invalid; continue; }
        const auto comment = value.toObject();
        if (!comment.value("p").isString() || !comment.value("m").isString()) { ++result.invalid; continue; }
        const auto fields = comment.value("p").toString().split(',');
        bool timeOk{}, modeOk{}, colorOk{};
        const auto time = fields.value(0).toDouble(&timeOk);
        const auto mode = fields.value(1).toInt(&modeOk);
        const auto colorText = fields.value(2).trimmed();
        auto color = colorText.toULongLong(&colorOk);
        if (fields.size() >= 3 && (colorText.isEmpty() || colorText.compare("null", Qt::CaseInsensitive) == 0 ||
                                  colorText.compare("undefined", Qt::CaseInsensitive) == 0)) {
            color = 0xffffff; colorOk = true;
        }
        appendItem(result, time, timeOk, mode, modeOk, comment.value("m").toString(), color, colorOk);
    }
    finishItems(result, stop);
    if (!result.cancelled && result.items.empty()) result.error = QStringLiteral("该剧集没有可播放的弹幕");
    return result;
}
OnlineDanmakuResult readCachedDanmaku(const QString& directory, const QString& key, std::stop_token stop) {
    OnlineDanmakuResult result;
    const auto path = cachePath(directory, key);
    if (path.isEmpty()) { result.error = QStringLiteral("无效的缓存条目"); return result; }
    const auto bytes = readLimited(path, result.error, stop);
    if (stop.stop_requested()) { result.cancelled = true; return result; }
    if (!result.error.isEmpty()) return result;
    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) { result.error = QStringLiteral("缓存 JSON 损坏"); return result; }
    auto object = doc.object();
    const auto rawComments = object.take("comments");
    for (const auto& field : {"version", "server", "animeId", "episodeId", "animeTitle", "episodeTitle", "downloadedAt", "count"})
        result.metadata[field] = object.value(field).toVariant();
    result.metadata["key"] = key;
    if (object.value("version").toInt(-1) != 1) { result.error = QStringLiteral("不支持的缓存版本"); return result; }
    bool episodeOk{}, animeOk{};
    const auto episodeId = result.metadata.value("episodeId").toLongLong(&episodeOk);
    const auto animeId = result.metadata.value("animeId").toLongLong(&animeOk);
    const auto server = normalizeDanmakuServer(object.value("server").toString());
    if (!episodeOk || episodeId <= 0 || !animeOk || animeId <= 0 || server.isEmpty() ||
        key != danmakuCacheKey(server, episodeId) || object.value("animeTitle").toString().isEmpty() ||
        object.value("episodeTitle").toString().isEmpty() ||
        !QDateTime::fromString(object.value("downloadedAt").toString(), Qt::ISODateWithMs).isValid() ||
        !rawComments.isArray()) { result.error = QStringLiteral("缓存元数据损坏"); return result; }
    const auto comments = rawComments.toArray();
    if (comments.size() > maxComments) { result.error = QStringLiteral("缓存弹幕超过 100 万条"); return result; }
    for (const auto& value : comments) {
        if (stop.stop_requested()) { result.cancelled = true; result.items.clear(); return result; }
        const auto c = value.toObject();
        const auto before = result.items.size();
        const auto color = c.value("color").toDouble(-1);
        appendItem(result, c.value("time").toDouble(-1), c.value("time").isDouble(), c.value("mode").toInt(-1),
                   c.value("mode").isDouble(), c.value("text").toString(),
                   color >= 0 && color <= 0xffffff ? static_cast<qulonglong>(color) : 0,
                   c.value("color").isDouble() && color >= 0 && color <= 0xffffff && std::floor(color) == color,
                   c.value("fontSize").toInt(-1));
        if (result.items.size() == before) { result.error = QStringLiteral("缓存弹幕数据损坏"); result.items.clear(); return result; }
    }
    finishItems(result, stop);
    if (!result.cancelled && (result.items.empty() || object.value("count").toInteger(-1) !=
        static_cast<qint64>(result.items.size()) || result.duplicates > 0)) {
        result.error = QStringLiteral("缓存条数不一致或没有弹幕"); result.items.clear();
    }
    return result;
}
QString saveCachedDanmaku(const QString& directory, const OnlineDanmakuResult& result, std::stop_token stop) {
    if (stop.stop_requested()) return {};
    if (QFileInfo(directory).isSymLink() || !QDir().mkpath(directory)) return QStringLiteral("无法创建弹幕缓存目录");
    const auto key = danmakuCacheKey(result.metadata.value("server").toString(), result.metadata.value("episodeId").toLongLong());
    const auto path = cachePath(directory, key);
    if (path.isEmpty()) return QStringLiteral("无效的缓存路径");
    auto object = QJsonObject::fromVariantMap(result.metadata);
    object.remove("key"); object["version"] = 1;
    object["count"] = static_cast<qint64>(result.items.size());
    QJsonArray comments;
    for (const auto& item : result.items) {
        if (stop.stop_requested()) return {};
        comments.append(QJsonObject{{"time", item.time}, {"mode", static_cast<int>(item.mode)},
                                    {"text", QString::fromUtf8(item.text)}, {"color", static_cast<qint64>(item.color)},
                                    {"fontSize", item.fontSize}});
    }
    object["comments"] = comments;
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (bytes.size() > maxCacheBytes) return QStringLiteral("规范化缓存超过 128 MiB 限制");
    if (stop.stop_requested()) return {};
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    for (qsizetype offset = 0; offset < bytes.size(); offset += 256 * 1024) {
        if (stop.stop_requested()) { file.cancelWriting(); return {}; }
        const auto count = std::min<qsizetype>(256 * 1024, bytes.size() - offset);
        if (file.write(bytes.constData() + offset, count) != count) { file.cancelWriting(); return file.errorString(); }
    }
    if (stop.stop_requested()) { file.cancelWriting(); return {}; }
    return file.commit() ? QString() : file.errorString();
}
QVariantList listCachedDanmaku(const QString& directory, std::stop_token stop) {
    QVariantList entries;
    if (QFileInfo(directory).isSymLink()) return entries;
    const auto files = QDir(directory).entryInfoList({"*.json"}, QDir::Files | QDir::NoSymLinks, QDir::Name);
    for (const auto& file : files) {
        if (stop.stop_requested()) return {};
        const auto key = file.completeBaseName();
        if (cachePath(directory, key).isEmpty()) continue;
        auto result = readCachedDanmaku(directory, key, stop);
        if (result.cancelled) return {};
        auto entry = result.metadata;
        entry["key"] = key;
        entry["valid"] = result.error.isEmpty();
        entry["error"] = result.error;
        if (entry.value("animeTitle").toString().isEmpty()) entry["animeTitle"] = QStringLiteral("损坏的缓存");
        if (entry.value("episodeTitle").toString().isEmpty()) entry["episodeTitle"] = key.left(12);
        entries.append(entry);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.toMap().value("downloadedAt").toString() > b.toMap().value("downloadedAt").toString();
    });
    return entries;
}
QString deleteCachedDanmaku(const QString& directory, const QString& key) {
    const auto path = cachePath(directory, key);
    if (path.isEmpty()) return QStringLiteral("无效的缓存条目");
    if (!QFileInfo::exists(path)) return {};
    QFile file(path);
    return file.remove() ? QString() : file.errorString();
}
