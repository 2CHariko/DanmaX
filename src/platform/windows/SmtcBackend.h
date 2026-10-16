#pragma once
#include "infrastructure/MediaPlaybackBackend.h"
#include <mutex>
#include <thread>
#include <deque>

class SmtcBackend final : public MediaPlaybackBackend {
    Q_OBJECT
  public:
    explicit SmtcBackend(QObject* parent = nullptr);
    ~SmtcBackend() override;
    bool accepts(const QString& id) const override { return !id.startsWith("danmax-vlc:"); }
    void select(const QString& id, quint64 generation) override;
    void requestPlayback(MediaSample expected, bool play, quint64 request) override;
  private:
    void run(std::stop_token stop);
    std::mutex mutex_;
    QString selected_;
    quint64 generation_{};
    struct Command { MediaSample expected; bool play{}; quint64 request{}; };
    std::deque<Command> commands_;
    std::jthread worker_;
};
