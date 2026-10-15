#include "infrastructure/MetadataCache.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QDebug>
#include <algorithm>

namespace {
constexpr qint64 maxJson = 16LL * 1024 * 1024;
constexpr qint64 maxCover = 8LL * 1024 * 1024;
}
MetadataCache::MetadataCache(QString root, QObject* parent)
    : MetadataCache(std::move(root), Policy{}, parent) {}
MetadataCache::MetadataCache(QString root, Policy policy, QObject* parent)
    : QObject(parent), root_(QDir::cleanPath(QDir(root).absoluteFilePath("metadata"))), policy_(std::move(policy)) {
    queue_.setMaxThreadCount(1);
    inspect();
}
MetadataCache::~MetadataCache() { queue_.waitForDone(); ++epoch_; }
QString MetadataCache::digest(const QByteArray& value) {
    return QString::fromLatin1(QCryptographicHash::hash(value, QCryptographicHash::Sha256).toHex());
}
QString MetadataCache::directory(bool cover) const { return QDir(root_).filePath(cover ? "covers" : "json"); }
bool MetadataCache::safeDirectory(const QString& path) const {
    const auto clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    if (clean != root_ && !clean.startsWith(root_ + '/')) return false;
    // Reject junctions/reparse points in the managed subtree, including metadata itself.
    for (QString current = clean;; current = QFileInfo(current).absolutePath()) {
        const QFileInfo info(current);
        if (info.isSymLink() || info.isJunction()) return false;
        if (current == root_) break;
    }
    return true;
}
bool MetadataCache::fresh(const Record& record, qint64 seconds) const {
    const auto current = now();
    const auto age = record.fetchedAt.msecsTo(current);
    return record.valid && record.fetchedAt.isValid() && current.isValid() && age >= 0 && age < seconds * 1000;
}
bool MetadataCache::writeFile(const QString& path, const QByteArray& bytes) {
    if (!safeDirectory(QFileInfo(path).absolutePath()) || QFileInfo(path).isSymLink()) return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        qWarning() << "Metadata cache write failed:" << path << file.errorString();
        return false;
    }
    return true;
}
MetadataCache::Record MetadataCache::readFile(const QString& path, const QString& key, bool cover, bool touch) {
    Record record;
    if (!safeDirectory(QFileInfo(path).absolutePath()) || QFileInfo(path).isSymLink()) return record;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > maxJson + 65536) return record;
    const auto doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    auto object = doc.object();
    if (!doc.isObject() || object.value("version").toInt() != 1 || object.value("key").toString() != key
        || object.value("cover").toBool() != cover) return record;
    record.fetchedAt = QDateTime::fromString(object.value("fetchedAt").toString(), Qt::ISODateWithMs).toUTC();
    if (!record.fetchedAt.isValid()) return {};
    if (cover) {
        const auto blob = object.value("blob").toString();
        if (blob != digest(key.toUtf8()) + '-' + object.value("contentHash").toString() + ".img"
            || object.value("contentHash").toString().size() != 64 || blob.contains('/') || blob.contains('\\')) return {};
        const auto blobPath = QDir(directory(true)).filePath(blob);
        QFile image(blobPath);
        if (QFileInfo(blobPath).isSymLink() || !image.open(QIODevice::ReadOnly) || image.size() > maxCover) return {};
        record.data = image.readAll();
        if (digest(record.data) != object.value("contentHash").toString()) return {};
        record.localUrl = QUrl::fromLocalFile(blobPath);
    } else {
        const auto payload = object.value("payload");
        if (!payload.isArray() && !payload.isObject()) return {};
        record.data = (payload.isArray() ? QJsonDocument(payload.toArray()) : QJsonDocument(payload.toObject())).toJson(QJsonDocument::Compact);
        if (record.data.size() > maxJson) return {};
    }
    record.valid = true;
    if (touch) { object["lastUsed"] = now().toString(Qt::ISODateWithMs); writeFile(path, QJsonDocument(object).toJson(QJsonDocument::Compact)); }
    return record;
}
void MetadataCache::deliver(quint64 epoch, Callback callback, Record record) {
    QMetaObject::invokeMethod(this, [this, epoch, callback = std::move(callback), record = std::move(record)]() mutable {
        if (epoch == epoch_ && callback) callback(std::move(record));
    }, Qt::QueuedConnection);
}
void MetadataCache::read(const QString& key, bool cover, Callback callback) {
    const auto epoch = epoch_.load();
    queue_.start([this, epoch, key, cover, callback = std::move(callback)]() mutable {
        if (epoch != epoch_) return;
        deliver(epoch, std::move(callback), readFile(QDir(directory(cover)).filePath(digest(key.toUtf8()) + ".json"), key, cover, true));
    });
}
void MetadataCache::write(const QString& key, bool cover, QByteArray data, QDateTime fetchedAt, Callback callback) {
    const auto epoch = epoch_.load();
    const auto pins = pins_;
    const auto coverFiles = coverFiles_;
    queue_.start([this, epoch, key, cover, data = std::move(data), fetchedAt, pins, coverFiles, callback = std::move(callback)]() mutable {
        if (epoch != epoch_) return;
        Record record{data, fetchedAt, {}, true};
        const auto dir = directory(cover);
        bool saved = safeDirectory(dir) && QDir().mkpath(dir);
        QJsonObject object{{"version", 1}, {"key", key}, {"cover", cover},
            {"fetchedAt", fetchedAt.toUTC().toString(Qt::ISODateWithMs)}, {"lastUsed", now().toString(Qt::ISODateWithMs)}};
        const auto path = QDir(dir).filePath(digest(key.toUtf8()) + ".json");
        QString blobPath;
        if (cover) {
            object["contentHash"] = digest(data);
            const auto name = digest(key.toUtf8()) + '-' + digest(data) + ".img";
            object["blob"] = name;
            blobPath = QDir(dir).filePath(name);
            saved = saved && data.size() <= maxCover;
        } else {
            const auto payload = QJsonDocument::fromJson(data);
            saved = saved && data.size() <= maxJson && (payload.isObject() || payload.isArray());
            object["payload"] = payload.isArray() ? QJsonValue(payload.array()) : QJsonValue(payload.object());
        }
        const auto envelope = QJsonDocument(object).toJson(QJsonDocument::Compact);
        qint64 pinnedBytes = 0;
        QSet<QString> countedBlobs;
        // If protected entries already fill the budget, keep this response session-only.
        for (const auto& pin : pins) {
            if (pin == key) continue;
            const auto pinPath = QDir(dir).filePath(digest(pin.toUtf8()) + ".json");
            QFile file(pinPath);
            if (!file.open(QIODevice::ReadOnly) || file.size() > maxJson + 65536) continue;
            pinnedBytes += file.size();
            if (cover) {
                const auto blob = QJsonDocument::fromJson(file.readAll()).object().value("blob").toString();
                if (!blob.contains('/') && !blob.contains('\\')) {
                    pinnedBytes += QFileInfo(QDir(dir).filePath(blob)).size(); countedBlobs.insert(blob);
                }
            }
        }
        if (cover) for (const auto& blob : coverFiles) {
            if (!countedBlobs.contains(blob) && blob != QFileInfo(blobPath).fileName()
                && !blob.contains('/') && !blob.contains('\\')) pinnedBytes += QFileInfo(QDir(dir).filePath(blob)).size();
        }
        saved = saved && pinnedBytes + envelope.size() + (cover ? data.size() : 0) <= (cover ? policy_.coverBudget : policy_.jsonBudget);
        if (cover && saved) saved = writeFile(blobPath, data);
        saved = saved && writeFile(path, envelope);
        if (cover && saved) record.localUrl = QUrl::fromLocalFile(blobPath);
        if (!saved) qWarning() << "Metadata cache entry was not persisted:" << key;
        maintain(pins, coverFiles);
        if (cover && !QFileInfo::exists(record.localUrl.toLocalFile())) record.localUrl = {};
        deliver(epoch, std::move(callback), std::move(record));
    });
}
void MetadataCache::maintain(const QSet<QString>& pins, const QSet<QString>& coverFiles) {
    qint64 totals[2]{};
    for (int kind = 0; kind < 2; ++kind) {
        const auto dir = directory(kind != 0);
        if (!safeDirectory(dir)) continue;
        struct Entry { QString path, blob, key; QDateTime used; qint64 size; };
        QList<Entry> entries;
        QSet<QString> referenced;
        for (const auto& info : QDir(dir).entryInfoList({"*.json"}, QDir::Files | QDir::NoSymLinks)) {
            QFile file(info.filePath());
            if (info.size() > maxJson + 65536) {
                if (!QFile::remove(info.filePath())) totals[kind] += info.size();
                continue;
            }
            if (!file.open(QIODevice::ReadOnly)) { totals[kind] += info.size(); continue; }
            const auto obj = QJsonDocument::fromJson(file.readAll()).object();
            file.close();
            const auto key = obj.value("key").toString();
            if (obj.value("version").toInt() != 1 || obj.value("cover").toBool() != (kind != 0)
                || !QDateTime::fromString(obj.value("fetchedAt").toString(), Qt::ISODateWithMs).isValid()
                || info.fileName() != digest(key.toUtf8()) + ".json"
                || (!kind && !obj.value("payload").isArray() && !obj.value("payload").isObject())) {
                if (!QFile::remove(info.filePath())) totals[kind] += info.size();
                continue;
            }
            QString blob;
            qint64 size = info.size();
            if (kind) {
                blob = obj.value("blob").toString();
                const auto hash = obj.value("contentHash").toString();
                const QFileInfo blobInfo(QDir(dir).filePath(blob));
                if (hash.size() != 64 || blob != digest(key.toUtf8()) + '-' + hash + ".img"
                    || blob.contains('/') || blob.contains('\\') || !blobInfo.isFile() || blobInfo.isSymLink()
                    || blobInfo.size() > maxCover) {
                    if (!QFile::remove(info.filePath())) totals[kind] += info.size();
                    continue;
                }
                referenced.insert(blob); size += blobInfo.size();
            }
            entries.append({info.filePath(), blob, key, QDateTime::fromString(obj.value("lastUsed").toString(), Qt::ISODateWithMs), size});
            totals[kind] += size;
        }
        if (kind) for (const auto& info : QDir(dir).entryInfoList({"*.img"}, QDir::Files | QDir::NoSymLinks))
            if (!referenced.contains(info.fileName())) {
                if (coverFiles.contains(info.fileName()) || !QFile::remove(info.filePath())) totals[kind] += info.size();
            }
        std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.used < b.used; });
        const auto budget = kind ? policy_.coverBudget : policy_.jsonBudget;
        for (const auto& entry : entries) {
            if (totals[kind] <= budget) break;
            if (pins.contains(entry.key)) continue;
            if (QFile::remove(entry.path)) {
                if (!entry.blob.isEmpty()) QFile::remove(QDir(dir).filePath(entry.blob));
                totals[kind] -= entry.size;
            }
        }
    }
    QMetaObject::invokeMethod(this, [this, json = totals[0], covers = totals[1]] { emit usageChanged(json, covers); }, Qt::QueuedConnection);
}
void MetadataCache::inspect() {
    const auto pins = pins_; const auto files = coverFiles_;
    queue_.start([this, pins, files] { maintain(pins, files); });
}
void MetadataCache::clear(std::function<void(QString)> callback) {
    const auto epoch = ++epoch_;
    queue_.start([this, epoch, callback = std::move(callback)] {
        QString error;
        for (const auto& dir : {directory(false), directory(true)}) {
            if (!safeDirectory(dir)) { error = QStringLiteral("缓存目录包含链接，已停止清理"); continue; }
            for (const auto& info : QDir(dir).entryInfoList(QDir::Files | QDir::NoSymLinks))
                if ((info.suffix() == "json" || info.suffix() == "img") && !QFile::remove(info.filePath())) error = QStringLiteral("部分缓存文件无法删除");
        }
        const auto legacy = QDir(root_).filePath("calendar.json");
        if (safeDirectory(root_) && !QFileInfo(legacy).isSymLink() && QFileInfo::exists(legacy) && !QFile::remove(legacy)) error = QStringLiteral("旧日历缓存无法删除");
        maintain({}, {});
        QMetaObject::invokeMethod(this, [this, epoch, error, callback] { if (epoch == epoch_) callback(error); }, Qt::QueuedConnection);
    });
}
void MetadataCache::importCalendar(Callback callback) {
    const auto epoch = epoch_.load();
    queue_.start([this, epoch, callback = std::move(callback)]() mutable {
        Record record;
        const auto path = QDir(root_).filePath("calendar.json");
        QFile file(path);
        if (safeDirectory(root_) && !QFileInfo(path).isSymLink() && file.open(QIODevice::ReadOnly) && file.size() <= maxJson) {
            record.data = file.readAll(); record.fetchedAt = QFileInfo(file).lastModified().toUTC(); record.valid = true;
        }
        deliver(epoch, std::move(callback), record);
    });
}
void MetadataCache::finishCalendarMigration() {
    queue_.start([this] {
        const auto path = QDir(root_).filePath("calendar.json");
        if (safeDirectory(root_) && !QFileInfo(path).isSymLink()) QFile::remove(path);
    });
}
