#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <mutex>
#include <thread>
#include <deque>
struct MediaSample {
    enum class Playback { Unknown, Playing, Paused, Stopped, Closed, Changing };
    QString id, title, identity;
    double position{}, duration{}, rate{1};
    bool playing{}, found{};
    Playback playback{Playback::Unknown};
    quint64 generation{};
    quint64 sessionToken{};
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
    void requestPlayback(MediaSample expected, bool play, quint64 request);
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
    void playbackControlFinished(quint64 request, bool success, QString error);

  private:
    void run(std::stop_token stop);
    std::mutex mutex_;
    QString selected_;
    quint64 generation_{1};
    struct Command { MediaSample expected; bool play{}; quint64 request{}; };
    std::deque<Command> commands_;
    std::jthread worker_;
};
