#include "infrastructure/MediaAssociationStore.h"
#include "infrastructure/OnlineDanmakuData.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>
#include <cmath>

namespace {
QString key(const QString& kind, const QString& identity) {
    return kind + QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool valid(const QVariantMap& entry) {
    return entry.value("animeId").toLongLong() > 0 && entry.value("episodeId").toLongLong() > 0 &&
        !normalizeDanmakuServer(entry.value("server").toString()).isEmpty() &&
        entry.value("server").toString().size() <= 4096 &&
        !entry.value("animeTitle").toString().isEmpty() && entry.value("animeTitle").toString().size() <= 512 &&
        !entry.value("episodeTitle").toString().isEmpty() && entry.value("episodeTitle").toString().size() <= 512 &&
        !entry.value("sourceAnime").toString().isEmpty() && entry.value("sourceAnime").toString().size() <= 1024;
}
}
MediaAssociationStore::MediaAssociationStore(const QString& dataDirectory)
    : path_(QDir(dataDirectory).filePath("media-associations.json")) {
    QFile file(path_);
    if (!file.exists()) return;
    loadError_ = QStringLiteral("关联文件不可读、损坏或版本不受支持，原文件已保留");
    if (!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return;
    const auto root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value("version").toInt() != 1 || !root.value("entries").isObject()) return;
    const auto entries = root.value("entries").toObject();
    if (entries.size() > 512) return;
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        if (!it.value().isObject() || !valid(it.value().toObject().toVariantMap())) { entries_ = {}; return; }
        entries_.insert(it.key(), it.value());
    }
    loadError_.clear();
}
QVariantMap MediaAssociationStore::exact(const ParsedMediaTitle& title) const {
    for (const auto& animeKey : QStringList{title.animeKey} + title.legacyAnimeKeys) {
        auto legacy = title; legacy.animeKey = animeKey;
        const auto record = entries_.value(key("episode-", legacy.key())).toObject().toVariantMap();
        if (!record.isEmpty()) return record;
    }
    return {};
}
QVariantMap MediaAssociationStore::series(const ParsedMediaTitle& title) const {
    for (const auto& animeKey : QStringList{title.animeKey} + title.legacyAnimeKeys) {
        const auto record = entries_.value(key("series-", animeKey)).toObject().toVariantMap();
        if (!record.isEmpty()) return record;
    }
    return {};
}
QString MediaAssociationStore::remember(const ParsedMediaTitle& title, const QVariantMap& metadata) {
    QVariantMap entry;
    for (const auto* field : {"server", "animeId", "animeTitle", "episodeId", "episodeTitle", "episodeNumber"})
        entry[field] = metadata.value(field);
    entry["sourceAnime"] = title.animeKey;
    if (!title.valid() || !valid(entry)) return QStringLiteral("关联信息不完整，未保存");
    entry["key"] = danmakuCacheKey(entry.value("server").toString(), entry.value("episodeId").toLongLong());
    const auto episodeKey = key("episode-", title.key());
    const auto seriesKey = key("series-", title.animeKey);
    // Reserve room for both records without unbounded filename history.
    while (entries_.size() > 509) entries_.remove(entries_.begin().key());
    entries_[episodeKey] = QJsonObject::fromVariantMap(entry);
    const auto number = danmakuEpisodeNumber(entry.value("episodeTitle").toString(), entry.value("episodeNumber").toDouble());
    if (std::abs(number - title.episode) < 0.0001) entries_[seriesKey] = QJsonObject::fromVariantMap(entry);
    while (QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries_}}).toJson(QJsonDocument::Compact).size() > 512 * 1024 && entries_.size() > 2) {
        auto oldest = entries_.begin();
        while (oldest != entries_.end() && (oldest.key() == episodeKey || oldest.key() == seriesKey)) ++oldest;
        if (oldest == entries_.end()) break;
        entries_.erase(oldest);
    }
    return save();
}
QString MediaAssociationStore::forget(const ParsedMediaTitle& title) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if ((QStringList{title.animeKey} + title.legacyAnimeKeys).contains(it.value().toObject().value("sourceAnime").toString())) it = entries_.erase(it);
        else ++it;
    }
    return save();
}
QVariantMap MediaAssociationStore::raw(const QString& title) const {
    if (title.isEmpty() || title.size() > 1024) return {};
    return entries_.value(key("raw-", title)).toObject().toVariantMap();
}
QString MediaAssociationStore::rememberRaw(const QString& title, const QVariantMap& metadata, const QString& sourceAnime) {
    if (title.isEmpty() || title.size() > 1024) return QStringLiteral("原始标题超过关联长度限制，未保存本文件关联");
    QVariantMap entry;
    for (const auto* field : {"server", "animeId", "animeTitle", "episodeId", "episodeTitle", "episodeNumber"}) entry[field] = metadata.value(field);
    entry["sourceAnime"] = sourceAnime.isEmpty() ? key("raw-", title) : sourceAnime;
    entry["key"] = danmakuCacheKey(entry.value("server").toString(), entry.value("episodeId").toLongLong());
    if (!valid(entry)) return QStringLiteral("本文件关联不完整，未保存");
    while (entries_.size() >= 512) entries_.remove(entries_.begin().key());
    const auto recordKey = key("raw-", title);
    entries_[recordKey] = QJsonObject::fromVariantMap(entry);
    while (QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries_}}).toJson(QJsonDocument::Compact).size() > 512 * 1024 && entries_.size() > 1) {
        auto item = entries_.begin(); if (item.key() == recordKey) ++item;
        entries_.erase(item);
    }
    return save();
}
QString MediaAssociationStore::forgetRaw(const QString& title) {
    entries_.remove(key("raw-", title)); return save();
}
QString MediaAssociationStore::save() {
    if (!loadError_.isEmpty()) return loadError_;
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"entries", entries_}}).toJson(QJsonDocument::Compact);
    if (bytes.size() > 512 * 1024) return QStringLiteral("关联内容超过 512 KiB，未写入文件");
    QSaveFile file(path_);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        return QStringLiteral("无法保存媒体关联：") + file.errorString();
    return {};
}
