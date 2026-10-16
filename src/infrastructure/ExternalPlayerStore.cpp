#include "infrastructure/ExternalPlayerStore.h"
#include "infrastructure/ExternalPlayerDraft.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

QVariantMap ExternalPlayerStore::toMap(const ExternalPlayerProfile& p) {
    ExternalPlayerRegistry registry; const auto* adapter = registry.find(p.adapter);
    return {{"id", p.id}, {"adapter", p.adapter}, {"name", p.name}, {"executable", p.executable},
        {"synchronize", adapter && (adapter->managedPlayback() || !adapter->sessionId(p).isEmpty())},
        {"synchronizationLabel", adapter ? adapter->synchronizationLabel() : QString()},
        {"canDiscover", adapter && adapter->canDiscover()}};
}
ExternalPlayerStore::ExternalPlayerStore(const QString& directory, QObject* parent)
    : QObject(parent), profiles_(ExternalPlayerRegistry().presets()), path_(QDir(directory).filePath("player-presets.json")) {
    QFile file(path_);
    if (!file.exists()) { commit(profiles_, selectedId_); return; }
    const auto invalid = [this](const QString& message) { readable_ = false; error_ = message; };
    if (!file.open(QIODevice::ReadOnly)) { invalid(file.errorString()); return; }
    if (file.size() > 65536) { invalid(QStringLiteral("播放器配置过大，原文件保留")); return; }
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(file.readAll(), &parse);
    const auto root = document.object();
    if (file.error() != QFileDevice::NoError || parse.error != QJsonParseError::NoError || !document.isObject()
        || root.value("version").toDouble() != 1 || !root.value("defaultPreset").isString() || !root.value("paths").isObject()) {
        invalid(QStringLiteral("预设播放器配置格式或版本无效，原文件保留")); return;
    }
    const auto paths = root.value("paths").toObject();
    ExternalPlayerRegistry registry;
    const auto selected = root.value("defaultPreset").toString();
    if (!registry.find(selected)) { invalid(QStringLiteral("默认播放器预设不存在")); return; }
    for (auto it = paths.begin(); it != paths.end(); ++it) {
        if (!registry.find(it.key()) || !it.value().isString()) { invalid(QStringLiteral("播放器路径字段无效")); return; }
        const auto path = it.value().toString();
        if (path.contains(QChar::Null) || path.size() > 32768 || (!path.isEmpty() && !QFileInfo(path).isAbsolute())) {
            invalid(QStringLiteral("播放器路径格式无效")); return;
        }
    }
    // A disconnected or removed installation stays visible for manual repair;
    // selection, save and launch validate the actual file again.
    for (auto& p : profiles_) p.executable = paths.value(p.id).toString();
    selectedId_ = selected;
}
QVariantList ExternalPlayerStore::profiles() const {
    QVariantList result; for (const auto& p : profiles_) result.append(toMap(p)); return result;
}
ExternalPlayerProfile ExternalPlayerStore::current() const {
    if (!readable_) { ExternalPlayerProfile p; p.adapter.clear(); p.name = QStringLiteral("播放器配置不可用"); return p; }
    for (const auto& p : profiles_) if (p.id == selectedId_) return p;
    return {};
}
QVariantMap ExternalPlayerStore::selected() const { return toMap(current()); }
bool ExternalPlayerStore::reject(const QString& message) { error_ = message; emit changed(); return false; }
bool ExternalPlayerStore::commit(const QList<ExternalPlayerProfile>& profiles, const QString& selectedId) {
    if (!readable_) return reject(QStringLiteral("播放器配置读取失败；请先修复或移走原文件，未覆盖原内容"));
    QJsonObject paths;
    for (const auto& p : profiles) paths.insert(p.id, p.executable);
    if (!QDir().mkpath(QFileInfo(path_).absolutePath())) return reject(QStringLiteral("无法创建播放器配置目录"));
    QSaveFile file(path_);
    const auto bytes = QJsonDocument(QJsonObject{{"version", 1}, {"defaultPreset", selectedId}, {"paths", paths}}).toJson();
    if (bytes.size() > 65536) return reject(QStringLiteral("播放器配置过大，未保存此次变更"));
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) return reject(file.errorString());
    profiles_ = profiles; selectedId_ = selectedId; error_.clear(); ++revision_; emit changed(); return true;
}
QObject* ExternalPlayerStore::createEditor(QObject* owner) { return new ExternalPlayerDraft(this, owner ? owner : this); }
QVariantMap ExternalPlayerStore::saveDraft(const QVariantList& entries, const QString& selectedId, quint64 expectedRevision) {
    const auto failure = [](const QString& message, const QString& id = QString()) {
        return QVariantMap{{"ok", false}, {"error", message}, {"profileId", id}};
    };
    if (expectedRevision != revision_) return failure(QStringLiteral("播放器配置已更新，请重新打开管理窗口后编辑"));
    ExternalPlayerRegistry registry;
    auto next = registry.presets();
    if (entries.size() != next.size() || !registry.find(selectedId)) return failure(QStringLiteral("播放器预设列表无效"));
    QStringList ids;
    for (const auto& entry : entries) {
        const auto map = entry.toMap(); const auto id = map.value("id").toString();
        const auto* adapter = registry.find(id);
        if (!adapter || ids.contains(id) || map.value("executable").metaType().id() != QMetaType::QString)
            return failure(QStringLiteral("播放器预设或路径字段无效"), id);
        ids.append(id);
        const auto path = map.value("executable").toString();
        if (!path.isEmpty()) {
            const auto issue = adapter->validate(path);
            if (!issue.isEmpty()) return failure(issue, id);
        }
        for (auto& p : next) if (p.id == id) p.executable = path.isEmpty() ? QString() : QFileInfo(path).canonicalFilePath();
    }
    if (!commit(next, selectedId)) return failure(error_);
    return {{"ok", true}};
}
bool ExternalPlayerStore::setExecutable(const QString& path) {
    auto entries = profiles();
    for (auto& entry : entries) {
        auto map = entry.toMap();
        if (map.value("id") == selectedId_) { map["executable"] = path; entry = map; }
    }
    const auto result = saveDraft(entries, selectedId_, revision_);
    return result.value("ok").toBool() ? true : reject(result.value("error").toString());
}
