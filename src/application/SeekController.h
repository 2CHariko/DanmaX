#pragma once
#include "platform/windows/MediaMonitor.h"
#include <QObject>
#include <QTimer>
#include <optional>

// One product seek in flight per connection. New targets replace only the queued
// intent; uncertain delivery is never retried automatically.
class SeekController final : public QObject {
    Q_OBJECT
public:
    explicit SeekController(MediaMonitor* monitor, QObject* parent = nullptr,
                            int waitMs = 8000, int lateMs = 8000);
    void requestSeek(MediaSample expected, double seconds, quint64 request);
    void cancelAll();
    void observe(const MediaSample& sample);
signals:
    void dispatchRequested(MediaSample expected, double seconds, quint64 request);
    void finished(quint64 request, bool success, QString error);
    void landed(quint64 request, MediaSample actual, bool approximate);
private:
    struct Request {
        MediaSample expected, firstLanding;
        SeekDispatch dispatch;
        double target{}, started{}, expires{}, lastObserved{};
        quint64 id{};
        bool dispatched{};
        int stable{};
    };
    void start(Request request);
    void dispatched(const SeekDispatch& result);
    bool confirm(Request& request, const MediaSample& sample, bool& approximate);
    void complete(const MediaSample& sample, bool approximate);
    void tick();
    MediaMonitor* monitor_;
    QTimer timer_;
    std::optional<Request> current_, queued_, expired_;
    int waitMs_, lateMs_;
};
