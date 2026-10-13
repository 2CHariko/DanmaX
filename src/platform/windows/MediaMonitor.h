#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <mutex>
#include <thread>
struct MediaSample {
    enum class Playback { Unknown, Playing, Paused, Stopped, Closed, Changing };
    QString id, title, identity;
    double position{}, duration{}, rate{1};
    bool playing{}, found{};
    Playback playback{Playback::Unknown};
    quint64 generation{};
    double observedAt{}; // steady_clock seconds, including queue delay in freshness checks.
};
Q_DECLARE_METATYPE(MediaSample)
class MediaMonitor final : public QObject {
    Q_OBJECT
  public:
    explicit MediaMonitor(QObject* parent = nullptr);
    ~MediaMonitor() override;
    quint64 select(QString id, bool renew = false);
    quint64 generation();
    static double monotonicSeconds();
    static bool targetForeground(const QString& applicationId);
    static bool windowMatchesSession(quintptr handle, const QString& id);
    static QVariantMap processMetrics();
    static bool windowOrOwnerForeground(quintptr handle);
    static QVariantMap windowMetrics(quintptr handle);
  signals:
    void sessionsChanged(QVariantList sessions);
    void sampleReceived(MediaSample sample);
    void errorOccurred(QString error);

  private:
    void run(std::stop_token stop);
    std::mutex mutex_;
    QString selected_;
    quint64 generation_{1};
    std::jthread worker_;
};
