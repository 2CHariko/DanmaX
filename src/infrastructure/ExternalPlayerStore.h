#pragma once
#include "infrastructure/ExternalPlayer.h"
#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class ExternalPlayerStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY changed)
    Q_PROPERTY(QVariantMap selected READ selected NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
  public:
    explicit ExternalPlayerStore(const QString& directory, QObject* parent = nullptr);
    QVariantList profiles() const;
    QString selectedId() const { return selectedId_; }
    QVariantMap selected() const;
    ExternalPlayerProfile current() const;
    QString error() const { return error_; }
    quint64 revision() const { return revision_; }
    Q_INVOKABLE QObject* createEditor(QObject* owner);
    QVariantMap saveDraft(const QVariantList& profiles, const QString& selectedId, quint64 expectedRevision);
    bool setExecutable(const QString& path);
  signals:
    void changed();
  private:
    bool commit(const QList<ExternalPlayerProfile>& profiles, const QString& selectedId);
    bool reject(const QString& message);

    static QVariantMap toMap(const ExternalPlayerProfile& profile);
    QList<ExternalPlayerProfile> profiles_;
    QString selectedId_ = "potplayer", path_, error_;
    bool readable_ = true;
    quint64 revision_ = 0;
};
