#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantMap>
class SettingsStore final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
  public:
    explicit SettingsStore(QString directory, QObject* parent = nullptr);
    ~SettingsStore() override;
    void flushPending();
    QVariantMap values() const {
        return values_;
    }
    QString error() const {
        return error_;
    }
    static QVariantMap defaults();
    Q_INVOKABLE bool setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE bool setDanmakuServers(const QStringList& addresses) {
        return setValue(QStringLiteral("danmakuServers"), addresses);
    }
    Q_INVOKABLE bool reset();
    Q_INVOKABLE bool retrySave();
  signals:
    void changed();
    void errorChanged();

  private:
    bool save();
    void applyTheme();
    bool preserveOriginal_{};
    QTimer saveTimer_;
    QVariantMap values_;
    QString path_, error_;
};
