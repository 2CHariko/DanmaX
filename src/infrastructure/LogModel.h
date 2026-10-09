#pragma once
#include <QAbstractListModel>
#include <QString>
class LogModel final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    enum { TimeRole = Qt::UserRole + 1, LevelRole, MessageRole };
    explicit LogModel(QString directory, QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const {
        return static_cast<int>(rows_.size());
    }
    void configure(bool file, const QString& level);
    void append(const QString& level, const QString& message);
    Q_INVOKABLE void clear();
    Q_INVOKABLE int countForLevel(const QString& level) const;
    Q_INVOKABLE bool exportTo(const QString& path);
    static void install(LogModel* model);
    static void uninstall();
  signals:
    void countChanged();
    void writeFailed(const QString& message);

  private:
    struct Row {
        QString time, level, message;
    };
    QList<Row> rows_;
    QString file_;
    bool toFile_{true};
    int minimum_{1};
};
