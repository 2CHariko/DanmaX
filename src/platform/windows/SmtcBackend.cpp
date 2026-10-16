#include "platform/windows/SmtcBackend.h"
#include "platform/windows/SmtcReader.h"
#include <optional>
using namespace winrt;
namespace { QString qt(const hstring& text) { return QString::fromWCharArray(text.c_str(), text.size()); } }
SmtcBackend::SmtcBackend(QObject* parent) : MediaPlaybackBackend(parent) {
    worker_ = std::jthread([this](std::stop_token stop) { run(stop); });
}
SmtcBackend::~SmtcBackend() { worker_.request_stop(); if (worker_.joinable()) worker_.join(); }
void SmtcBackend::select(const QString& id, quint64 generation) {
    std::lock_guard lock(mutex_); if (generation_ != generation || selected_ != id) commands_.clear();
    selected_ = id; generation_ = generation;
}
void SmtcBackend::requestPlayback(MediaSample expected, bool play, quint64 request) {
    std::lock_guard lock(mutex_);
    if (commands_.size() < 8) commands_.push_back({std::move(expected), play, request});
}
void SmtcBackend::run(std::stop_token stop) {
    bool initialized = false;
    try {
        init_apartment(apartment_type::multi_threaded);
        initialized = true;
        // Scope all COM objects inside the apartment, including reconnect failures.
        SmtcReader reader;
        QString lastError;
        auto reportedAt = std::chrono::steady_clock::time_point::min();
        while (!stop.stop_requested()) {
            quint64 generation;
            QString selected;
            {
                std::lock_guard lock(mutex_);
                selected = selected_;
                generation = generation_;
            }
            auto result = reader.poll(selected, generation, stop);
            if (result.cancelled || stop.stop_requested()) break;
            if (result.publishSessions) emit sessionsChanged(result.sessions);
            if (!result.error.isEmpty()) {
                const auto now = std::chrono::steady_clock::now();
                if (lastError.isEmpty() || now - reportedAt >= std::chrono::seconds(5)) {
                    emit errorOccurred(result.error);
                    lastError = result.error;
                    reportedAt = now;
                }
            } else if (result.sample.found || result.publishSessions) {
                lastError.clear();
            }
            emit sampleReceived(result.sample);
            std::optional<Command> command;
            {
                std::lock_guard lock(mutex_);
                if (!commands_.empty()) { command = std::move(commands_.front()); commands_.pop_front(); }
            }
            if (command) {
                QString error;
                {
                    std::lock_guard lock(mutex_);
                    if (command->expected.generation != generation_ || command->expected.id != selected_)
                        error = QStringLiteral("选择的播放器已改变");
                }
                const auto current = [this, &command] {
                    std::lock_guard lock(mutex_);
                    return command->expected.generation == generation_ && command->expected.id == selected_;
                };
                if (error.isEmpty()) error = command->seek ? reader.seek(command->expected, command->seconds, stop, current)
                    : reader.control(command->expected, command->play, stop, current);
                if (command->seek) emit seekFinished(command->request, error.isEmpty(), error);
                else emit playbackControlFinished(command->request, error.isEmpty(), error);
            }
            for (int i = 0; i < 4 && !stop.stop_requested(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    } catch (const hresult_error& e) {
        emit errorOccurred(qt(e.message()));
    } catch (const std::exception& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
    }
    if (initialized) uninit_apartment();
}
void SmtcBackend::requestSeek(MediaSample expected, double seconds, quint64 request) {
    std::lock_guard lock(mutex_);
    if (commands_.size() < 8) commands_.push_back({std::move(expected), false, request, seconds, true});
    else emit seekFinished(request, false, QStringLiteral("控制队列已满"));
}
