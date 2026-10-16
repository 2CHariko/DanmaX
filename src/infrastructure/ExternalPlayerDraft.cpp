#include "infrastructure/ExternalPlayerDraft.h"
#include <QFileInfo>
#include <QUrl>

ExternalPlayerDraft::ExternalPlayerDraft(ExternalPlayerStore* store, QObject* parent, std::function<QStringList()> discover) : QAbstractListModel(parent), discover_(std::move(discover)), store_(store) { begin(); }
int ExternalPlayerDraft::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(entries_.size()); }
namespace {
QVariantMap withValidation(QVariantMap map) {
    ExternalPlayerRegistry registry; const auto* adapter = registry.find(map.value("id").toString());
    const auto path = map.value("executable").toString();
    const auto issue = path.isEmpty() ? QString() : adapter->validate(path);
    map["validation"] = path.isEmpty() ? QStringLiteral("未指定路径，打开视频时自动发现")
        : issue.isEmpty() ? QStringLiteral("路径验证通过 · Windows x64") : issue;
    map["pathValid"] = !path.isEmpty() && issue.isEmpty();
    return map;
}
}
QVariant ExternalPlayerDraft::data(const QModelIndex& index, int role) const {
    return index.isValid() && index.row() >= 0 && index.row() < entries_.size() && role == Qt::UserRole ? entries_[index.row()] : QVariant();
}int ExternalPlayerDraft::indexOf(const QString& id) const {
    for (int i = 0; i < entries_.size(); ++i) if (entries_[i].toMap().value("id").toString() == id) return i;
    return -1;
}
QVariantMap ExternalPlayerDraft::editing() const {
    const auto i = indexOf(editingId_); return i < 0 ? QVariantMap() : entries_[i].toMap();
}
bool ExternalPlayerDraft::canDiscover() const {
    ExternalPlayerRegistry registry; const auto* adapter = registry.find(editing().value("adapter").toString());
    return adapter && adapter->canDiscover();
}
QString ExternalPlayerDraft::executableFilter() const {
    ExternalPlayerRegistry registry; const auto* adapter = registry.find(editing().value("adapter").toString());
    return adapter ? adapter->executableFilter() : QStringLiteral("播放器 (*.exe)");
}
void ExternalPlayerDraft::begin() {
    beginResetModel(); entries_ = store_ ? store_->profiles() : QVariantList();
    for (auto& entry : entries_) entry = withValidation(entry.toMap());
    baseline_ = entries_;
    defaultId_ = store_ ? store_->selectedId() : QString(); baselineDefault_ = defaultId_;
    baseRevision_ = store_ ? store_->revision() : 0; editingId_.clear(); candidates_.clear(); issue_.clear(); issueProfileId_.clear();
    endResetModel(); emit changed();
}
void ExternalPlayerDraft::edit(const QString& id) {
    if (!id.isEmpty() && indexOf(id) < 0) return;
    if (editingId_ != id) candidates_.clear();
    editingId_ = id; emit changed();
}
void ExternalPlayerDraft::setDefault(const QString& id) {
    if (indexOf(id) < 0) return;
    defaultId_ = id; emit changed();
}
void ExternalPlayerDraft::fail(const QString& issue, const QString& id) { issue_ = issue; issueProfileId_ = id; emit changed(); }
void ExternalPlayerDraft::updatePath(const QString& path) {
    const auto i = indexOf(editingId_); if (i < 0) return;
    auto map = entries_[i].toMap();
    map["executable"] = path; entries_[i] = withValidation(map); issue_.clear(); issueProfileId_.clear(); candidates_.clear();
    emit dataChanged(index(i), index(i), {Qt::UserRole}); emit changed();
}
void ExternalPlayerDraft::chooseExecutable(const QString& input) {
    const auto url = QUrl(input); const auto path = url.isLocalFile() ? url.toLocalFile() : input;
    ExternalPlayerRegistry registry; const auto* adapter = registry.find(editing().value("adapter").toString());
    if (!adapter) { fail(QStringLiteral("播放器适配器不存在"), editingId_); return; }
    const auto error = adapter->validate(path);
    if (!error.isEmpty()) { fail(error, editingId_); return; }
    updatePath(QFileInfo(path).canonicalFilePath());
}
void ExternalPlayerDraft::enterExecutable(const QString& text) {
    auto path = text.trimmed();
    if (path.size() >= 2 && path.startsWith('"') && path.endsWith('"')) path = path.mid(1, path.size() - 2);
    const QUrl url(path);
    if (url.isLocalFile()) path = url.toLocalFile();
    if (path.isEmpty()) { updatePath({}); return; }
    ExternalPlayerRegistry registry; const auto* adapter = registry.find(editing().value("adapter").toString());
    if (!adapter) return;
    const auto issue = adapter->validate(path);
    // Keep invalid text in the draft for correction; the saved path stays intact.
    updatePath(issue.isEmpty() ? QFileInfo(path).canonicalFilePath() : path);
    if (!issue.isEmpty()) fail(issue, editingId_);
}
void ExternalPlayerDraft::discover() {
    ExternalPlayerRegistry registry; const auto* adapter = registry.find(editing().value("adapter").toString());
    if (!adapter || !adapter->canDiscover()) { fail(QStringLiteral("此播放器请手动选择程序"), editingId_); return; }
    candidates_.clear();
    const auto found = discover_ ? discover_() : adapter->discoverCandidates();
    for (const auto& path : found) {
        if (!adapter->validate(path).isEmpty()) continue;
        const auto canonical = QFileInfo(path).canonicalFilePath();
        if (!candidates_.contains(canonical, Qt::CaseInsensitive)) candidates_.append(canonical);
    }
    if (candidates_.isEmpty()) { fail(QStringLiteral("未发现有效的安装，请手动选择程序"), editingId_); return; }
    if (candidates_.size() == 1) { const auto path = candidates_.first(); chooseExecutable(path); return; }
    fail(QStringLiteral("发现多个安装位置，请选择要使用的程序"), editingId_);
}
void ExternalPlayerDraft::chooseCandidate(int index) {
    if (index >= 0 && index < candidates_.size()) { const auto path = candidates_[index]; chooseExecutable(path); }
}
bool ExternalPlayerDraft::save() {
    if (!store_) { fail(QStringLiteral("播放器配置服务已关闭")); return false; }
    const auto result = store_->saveDraft(entries_, defaultId_, baseRevision_);
    if (!result.value("ok").toBool()) {
        const auto id = result.value("profileId").toString();
        if (indexOf(id) >= 0) editingId_ = id;
        fail(result.value("error").toString(), id); return false;
    }
    begin(); return true;
}
