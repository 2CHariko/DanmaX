#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <mutex>
#include <thread>
struct MediaSample {
    QString id, title, identity;
    double position{}, duration{}, rate{1};
    bool playing{}, found{};
};
Q_DECLARE_METATYPE(MediaSample)
class MediaMonitor final : public QObject {
    Q_OBJECT
  public:
    explicit MediaMonitor(QObject* parent = nullptr);
    ~MediaMonitor() override;
    void select(QString id);
    static bool targetForeground(const QString& applicationId);
    static bool windowMatchesSession(quintptr handle, const QString& id);
    static QVariantMap processMetrics();
    static void maintainTopmost(quintptr handle, int strategy);
    static QVariantMap windowMetrics(quintptr handle);
  signals:
    void sessionsChanged(QVariantList sessions);
    void sampleReceived(MediaSample sample);
    void errorOccurred(QString error);

  private:
    void run(std::stop_token stop);
    std::mutex mutex_;
    QString selected_;
    std::jthread worker_;
};
