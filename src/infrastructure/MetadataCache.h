#pragma once
#include <QObject>
#include <QDateTime>
#include <QSet>
#include <QHash>
#include <QThreadPool>
#include <QUrl>
#include <atomic>
#include <functional>

// All disk operations run on one queue. Entry files are their own recoverable index.
class MetadataCache final : public QObject {
    Q_OBJECT
public:
    struct Policy {
        qint64 jsonBudget = 64LL * 1024 * 1024;
        qint64 coverBudget = 512LL * 1024 * 1024;
        std::function<QDateTime()> now = [] { return QDateTime::currentDateTimeUtc(); };
    };
    struct Record {
        QByteArray data;
        QDateTime fetchedAt;
        QUrl localUrl;
        bool valid = false;
    };
    using Callback = std::function<void(Record)>;
    explicit MetadataCache(QString root, QObject* parent = nullptr);
    MetadataCache(QString root, Policy policy, QObject* parent = nullptr);
    ~MetadataCache() override;
    static QString digest(const QByteArray& value);
    QDateTime now() const { return policy_.now().toUTC(); }
    bool fresh(const Record& record, qint64 seconds) const;
    void read(const QString& key, bool cover, Callback callback);
    void write(const QString& key, bool cover, QByteArray data, QDateTime fetchedAt,
               Callback callback = {});
    void importCalendar(Callback callback);
    void finishCalendarMigration();
    void setPins(const QString& owner, QSet<QString> pins) {
        pinGroups_[owner] = std::move(pins); pins_.clear();
        for (const auto& group : pinGroups_) pins_.unite(group);
    }
    void setCoverFiles(QSet<QString> files) { coverFiles_ = std::move(files); }
    void clear(std::function<void(QString)> callback);
    void inspect();
signals:
    void usageChanged(qint64 jsonBytes, qint64 coverBytes);
private:
    QString directory(bool cover) const;
    bool safeDirectory(const QString& path) const;
    Record readFile(const QString& path, const QString& key, bool cover, bool touch);
    bool writeFile(const QString& path, const QByteArray& bytes);
    void maintain(const QSet<QString>& pins, const QSet<QString>& coverFiles);
    void deliver(quint64 epoch, Callback callback, Record record);
    QString root_;
    Policy policy_;
    QThreadPool queue_;
    QSet<QString> pins_;
    QSet<QString> coverFiles_;
    QHash<QString, QSet<QString>> pinGroups_;
    std::atomic<quint64> epoch_{0};
};
