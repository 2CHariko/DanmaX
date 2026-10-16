#include "infrastructure/RecentVideoStore.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <cmath>

namespace {
constexpr qsizetype maxBytes = 8 * 1024 * 1024;
QString key(const QString& path) { return RecentVideoStore::normalizedPath(path).toCaseFolded(); }
bool sameFile(const QVariantMap& entry, const QFileInfo& file) {
    return entry.value("size").toString() == QString::number(file.size())
        && entry.value("modified").toString() == QString::number(file.lastModified().toMSecsSinceEpoch());
}
}
QString RecentVideoStore::normalizedPath(const QString& input) {
    const QUrl url(input); const auto path = url.isLocalFile() ? url.toLocalFile() : input;
    const QFileInfo file(path);
    if (path.contains(QChar::Null) || path.size() > 32768 || !file.isAbsolute()) return {};
    return QDir::fromNativeSeparators(file.exists() ? file.canonicalFilePath() : QDir::cleanPath(file.absoluteFilePath()));
}
RecentVideoStore::RecentVideoStore(const QString& directory, QObject* parent)
    : QAbstractListModel(parent), path_(QDir(directory).filePath("recent-videos.json")) {
    QFile file(path_); if (!file.exists()) return;
    const auto reject = [this] { writable_ = false; error_ = QStringLiteral("最近播放记录不可用，已保留原文件；仍可打开视频。"); };
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxBytes) { reject(); return; }
    QJsonParseError error; const auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject() || doc.object().value("version").toInt() != 1
        || !doc.object().value("entries").isArray() || doc.object().value("entries").toArray().size() > 100) { reject(); return; }
    QSet<QString> keys;
    for (const auto& value : doc.object().value("entries").toArray()) {
        const auto entry = value.toObject().toVariantMap(); const auto path = entry.value("path").toString();
        const auto position = entry.value("position").toDouble(), duration = entry.value("duration").toDouble();
        if (!value.isObject() || normalizedPath(path).isEmpty() || keys.contains(key(path)) || !std::isfinite(position)
            || !std::isfinite(duration) || position < 0 || duration < 0 || position > duration || duration > 366 * 86400.0
            || !value.toObject().value("position").isDouble() || !value.toObject().value("duration").isDouble()
            || !value.toObject().value("source").isObject()) { reject(); entries_.clear(); return; }
        keys.insert(key(path)); entries_.append(entry);
    }
}
QVariantMap RecentVideoStore::lookup(const QString& input) const {
    for (const auto& value : entries_) {
        auto entry = value.toMap(); if (key(entry.value("path").toString()) != key(input)) continue;
        const QFileInfo file(normalizedPath(input));
        if (file.exists() && !sameFile(entry, file)) { entry["position"] = 0.0; entry["duration"] = 0.0; entry["source"] = QVariantMap{}; }
        return entry;
    }
    return {};
}
QVariantList RecentVideoStore::entries() const {
    QVariantList result;
    for (const auto& value : entries_) {
        const auto original = value.toMap(); auto entry = lookup(original.value("path").toString());
        entry["missing"] = !QFileInfo(entry.value("path").toString()).isFile();
        const auto position = entry.value("position").toDouble(), duration = entry.value("duration").toDouble();
        entry["canResume"] = position >= 5 && position < duration - 10;
        result.append(entry);
    }
    return result;
}
void RecentVideoStore::opened(const QString& input) {
    const auto path = normalizedPath(input); const QFileInfo file(path); if (path.isEmpty() || !file.isFile()) return;
    auto entry = lookup(path); entry["path"] = path; entry["title"] = file.fileName();
    entry["lastOpened"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    entry["size"] = QString::number(file.size()); entry["modified"] = QString::number(file.lastModified().toMSecsSinceEpoch());
    if (!entry.contains("position")) { entry["position"] = 0.0; entry["duration"] = 0.0; entry["source"] = QVariantMap{}; }
    int previous = -1;
    for (int i = 0; i < entries_.size(); ++i) if (key(entries_[i].toMap().value("path").toString()) == key(path)) { previous = i; break; }
    if (previous > 0) {
        beginMoveRows({}, previous, previous, {}, 0); entries_.move(previous, 0); endMoveRows();
    } else if (previous < 0) {
        beginInsertRows({}, 0, 0); entries_.prepend(entry); endInsertRows();
    }
    entries_[0] = entry; emit dataChanged(index(0), index(0), {Qt::UserRole});
    if (entries_.size() > 100) { beginRemoveRows({}, 100, static_cast<int>(entries_.size()) - 1); entries_ = entries_.mid(0, 100); endRemoveRows(); }
    save();
}
void RecentVideoStore::checkpoint(const QString& input, double position, double duration, const QVariantMap& source) {
    if (!std::isfinite(position) || !std::isfinite(duration) || duration <= 0 || duration > 366 * 86400.0 || position < 0 || position > duration) return;
    const auto path = normalizedPath(input); const QFileInfo file(path);
    for (int i = 0; i < entries_.size(); ++i) {
        auto& value = entries_[i];
        auto entry = value.toMap(); if (key(entry.value("path").toString()) != key(path)) continue;
        if (!sameFile(entry, file)) return;
        if (entry.value("position").toDouble() == position && entry.value("duration").toDouble() == duration
            && entry.value("source").toMap() == source) return;
        entry["position"] = position; entry["duration"] = duration; entry["source"] = source; value = entry;
        emit dataChanged(index(i), index(i), {Qt::UserRole}); save(); return;
    }
}
void RecentVideoStore::remove(const QString& path) {
    for (int i = static_cast<int>(entries_.size()); i-- > 0;) if (key(entries_[i].toMap().value("path").toString()) == key(path)) {
        beginRemoveRows({}, i, i); entries_.removeAt(i); endRemoveRows();
    }
    save();
}
QVariant RecentVideoStore::data(const QModelIndex& item, int role) const {
    if (!item.isValid() || item.row() < 0 || item.row() >= entries_.size() || role != Qt::UserRole) return {};
    const auto original = entries_[item.row()].toMap(); auto entry = lookup(original.value("path").toString());
    entry["missing"] = !QFileInfo(entry.value("path").toString()).isFile();
    entry["canResume"] = entry.value("position").toDouble() >= 5 && entry.value("position").toDouble() < entry.value("duration").toDouble() - 10;
    return entry;
}
void RecentVideoStore::refresh() {
    if (!entries_.isEmpty()) emit dataChanged(index(0), index(static_cast<int>(entries_.size()) - 1), {Qt::UserRole});
    emit changed();
}
void RecentVideoStore::save() {
    if (writable_) {
        const auto data = QJsonDocument(QJsonObject{{"version", 1}, {"entries", QJsonArray::fromVariantList(entries_)}}).toJson();
        QSaveFile file(path_);
        if (!QDir().mkpath(QFileInfo(path_).absolutePath()) || data.size() > maxBytes || !file.open(QIODevice::WriteOnly)
            || file.write(data) != data.size() || !file.commit()) error_ = QStringLiteral("最近播放记录保存失败，原文件保持不变。");
        else error_.clear();
    }
    emit changed();
}
