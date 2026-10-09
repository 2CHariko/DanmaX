#include "infrastructure/LogModel.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QPointer>
#include <QSaveFile>
#include <atomic>
#include <mutex>
namespace {
std::mutex handlerMutex;
QPointer<LogModel> target;
QtMessageHandler previous{};
std::atomic<int> queued{};
std::atomic<int> dropped{};
int rank(const QString& s) {
    return s == "DEBUG" ? 0 : s == "INFO" ? 1 : s == "WARNING" ? 2 : 3;
}
void handler(QtMsgType type, const QMessageLogContext&, const QString& text) {
    std::lock_guard lock(handlerMutex);
    if (!target)
        return;
    if (queued.load() >= 1000) {
        ++dropped;
        return;
    }
    ++queued;
    const QString level = type == QtDebugMsg     ? "DEBUG"
                          : type == QtInfoMsg    ? "INFO"
                          : type == QtWarningMsg ? "WARNING"
                                                 : "ERROR";
    QMetaObject::invokeMethod(
        target,
        [model = target, level, text = text.left(8192)] {
            --queued;
            if (model) {
                const int lost = dropped.exchange(0);
                if (lost)
                    model->append("WARNING", QStringLiteral("日志过载，合并丢弃 %1 条").arg(lost));
                model->append(level, text);
            }
        },
        Qt::QueuedConnection);
}
} // namespace
LogModel::LogModel(QString directory, QObject* parent) : QAbstractListModel(parent) {
    QDir().mkpath(directory);
    file_ = QDir(directory).filePath("app.log");
}
int LogModel::countForLevel(const QString& level) const {
    int result = 0;
    for (const auto& row : rows_)
        if (level.isEmpty() || row.level == level) ++result;
    return result;
}
int LogModel::rowCount(const QModelIndex& p) const {
    return p.isValid() ? 0 : count();
}
QVariant LogModel::data(const QModelIndex& i, int role) const {
    if (!i.isValid() || i.row() < 0 || i.row() >= count())
        return {};
    const auto& r = rows_[i.row()];
    return role == TimeRole      ? r.time
           : role == LevelRole   ? r.level
           : role == MessageRole ? r.message
                                 : QString{};
}
QHash<int, QByteArray> LogModel::roleNames() const {
    return {{TimeRole, "time"}, {LevelRole, "level"}, {MessageRole, "message"}};
}
void LogModel::configure(bool file, const QString& level) {
    toFile_ = file;
    minimum_ = rank(level);
}
void LogModel::append(const QString& level, const QString& message) {
    if (rank(level) < minimum_)
        return;
    if (rows_.size() >= 1000) {
        beginRemoveRows({}, 0, 99);
        rows_.erase(rows_.begin(), rows_.begin() + 100);
        endRemoveRows();
    }
    const Row row{QDateTime::currentDateTime().toString("HH:mm:ss"), level, message.left(8192)};
    beginInsertRows({}, count(), count());
    rows_.push_back(row);
    endInsertRows();
    emit countChanged();
    if (toFile_) {
        QFile file(file_);
        bool ok = true;
        if (file.size() > 2 * 1024 * 1024) {
            if (QFile::exists(file_ + ".1"))
                ok = QFile::remove(file_ + ".1");
            if (ok)
                ok = file.rename(file_ + ".1");
            file.setFileName(file_);
        }
        const auto bytes = QString("%1 [%2] %3\n").arg(row.time, row.level, row.message).toUtf8();
        if (ok)
            ok = file.open(QIODevice::WriteOnly | QIODevice::Append) && file.write(bytes) == bytes.size();
        if (!ok) {
            toFile_ = false;
            emit writeFailed(
                QStringLiteral("日志文件写入失败，已暂停写盘；检查目录权限后重新开启文件日志。"));
        }
    }
}
void LogModel::clear() {
    beginResetModel();
    rows_.clear();
    endResetModel();
    emit countChanged();
}
bool LogModel::exportTo(const QString& path) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    for (const auto& r : rows_)
        if (f.write(QString("%1 [%2] %3\n").arg(r.time, r.level, r.message).toUtf8()) < 0)
            return false;
    return f.commit();
}
void LogModel::install(LogModel* m) {
    std::lock_guard lock(handlerMutex);
    target = m;
    previous = qInstallMessageHandler(handler);
}
void LogModel::uninstall() {
    std::lock_guard lock(handlerMutex);
    qInstallMessageHandler(previous);
    target = nullptr;
}
