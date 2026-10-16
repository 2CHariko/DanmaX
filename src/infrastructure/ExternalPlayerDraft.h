#pragma once
#include "infrastructure/ExternalPlayerStore.h"
#include <QAbstractListModel>
#include <QPointer>
#include <functional>

class ExternalPlayerDraft final : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QStringList candidates READ candidates NOTIFY changed)
    Q_PROPERTY(QString defaultId READ defaultId NOTIFY changed)
    Q_PROPERTY(QString editingId READ editingId NOTIFY changed)
    Q_PROPERTY(QVariantMap editing READ editing NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(QString issue READ issue NOTIFY changed)
    Q_PROPERTY(QString issueProfileId READ issueProfileId NOTIFY changed)
    Q_PROPERTY(bool canDiscover READ canDiscover NOTIFY changed)
    Q_PROPERTY(QString executableFilter READ executableFilter NOTIFY changed)
  public:
    explicit ExternalPlayerDraft(ExternalPlayerStore* store, QObject* parent = nullptr, std::function<QStringList()> discover = {});
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, "profile"}}; }
    QString defaultId() const { return defaultId_; }
    QString editingId() const { return editingId_; }
    QVariantMap editing() const;
    bool dirty() const { return entries_ != baseline_ || defaultId_ != baselineDefault_; }
    QString issue() const { return issue_; }
    QString issueProfileId() const { return issueProfileId_; }
    bool canDiscover() const;
    QString executableFilter() const;
    Q_INVOKABLE void begin();
    Q_INVOKABLE void edit(const QString& id);
    Q_INVOKABLE void setDefault(const QString& id);

    Q_INVOKABLE void chooseCandidate(int index);
    QStringList candidates() const { return candidates_; }
    Q_INVOKABLE void chooseExecutable(const QString& url);
    Q_INVOKABLE void enterExecutable(const QString& text);
    Q_INVOKABLE void discover();
    Q_INVOKABLE bool save();
  signals:
    void changed();
  private:
    void updatePath(const QString& path);
    QStringList candidates_;
    std::function<QStringList()> discover_;
    int indexOf(const QString& id) const;
    void fail(const QString& issue, const QString& id = {});
    QPointer<ExternalPlayerStore> store_;
    QVariantList entries_, baseline_;
    QString defaultId_, baselineDefault_, editingId_, issue_, issueProfileId_;
    quint64 baseRevision_ = 0;
};
