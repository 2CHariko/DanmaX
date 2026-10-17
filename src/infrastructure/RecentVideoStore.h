#pragma once
#include <QAbstractListModel>
#include <QVariantList>

class RecentVideoStore final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    explicit RecentVideoStore(const QString& directory, QObject* parent = nullptr);
    QVariantList entries() const;
    QString error() const { return error_; }
    QVariantMap lookup(const QString& path) const;
    void opened(const QString& path);
    void checkpoint(const QString& path, double position, double duration, const QVariantMap& source, const QVariantMap& content = {});
    Q_INVOKABLE void remove(const QString& path);
    static QString normalizedPath(const QString& input);
    int rowCount(const QModelIndex& parent = {}) const override { return parent.isValid() ? 0 : static_cast<int>(entries_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "entry"}}; }
    Q_INVOKABLE void refresh();
  signals:
    void changed();
  private:
    void save();
    QString path_, error_;
    QVariantList entries_;
    bool writable_{true};
};
