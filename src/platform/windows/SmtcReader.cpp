#include "platform/windows/SmtcReader.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <winrt/Windows.Foundation.Collections.h>

using namespace winrt;
using namespace Windows::Media::Control;
namespace {
QString qt(const hstring& text) {
    return QString::fromWCharArray(text.c_str(), static_cast<qsizetype>(text.size()));
}
template <class T> void require(const T& value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template <class Operation> bool waitFor(Operation& op, std::stop_token stop) {
    require(op, "SMTC returned a null asynchronous operation");
    for (int i = 0; i < 60; ++i) {
        if (stop.stop_requested()) {
            op.Cancel();
            return false;
        }
        if (op.Status() != Windows::Foundation::AsyncStatus::Started) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    op.Cancel();
    throw std::runtime_error("Windows media query timed out");
}
MediaSample readSample(const GlobalSystemMediaTransportControlsSession& session,
                       const GlobalSystemMediaTransportControlsSessionMediaProperties& properties,
                       MediaSample sample) {
    require(properties, "SMTC returned null media properties");
    const auto timeline = session.GetTimelineProperties();
    require(timeline, "SMTC returned null timeline properties");
    const auto playback = session.GetPlaybackInfo();
    require(playback, "SMTC returned null playback info");
    const auto start = timeline.StartTime();
    sample.duration = std::max(0.0, std::chrono::duration<double>(timeline.EndTime() - start).count());
    sample.position = std::chrono::duration<double>(timeline.Position() - start).count();
    switch (playback.PlaybackStatus()) {
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
        sample.playback = MediaSample::Playback::Playing; break;
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
        sample.playback = MediaSample::Playback::Paused; break;
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
        sample.playback = MediaSample::Playback::Stopped; break;
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed:
        sample.playback = MediaSample::Playback::Closed; break;
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing:
        sample.playback = MediaSample::Playback::Changing; break;
    default: sample.playback = MediaSample::Playback::Unknown; break;
    }
    sample.playing = sample.playback == MediaSample::Playback::Playing;
    if (const auto rate = playback.PlaybackRate()) {
        const auto value = rate.Value();
        if (std::isfinite(value)) sample.rate = std::clamp(value, 0.0, 8.0);
    }
    const double age = std::chrono::duration<double>(clock::now() - timeline.LastUpdatedTime()).count();
    if (sample.playing && age > 0 && age < 86400) sample.position += age * sample.rate;
    sample.position = std::max(0.0, sample.position);
    if (sample.duration > 0) sample.position = std::min(sample.position, sample.duration);
    sample.title = qt(properties.Title());
    sample.identity = sample.title + "\n" + qt(properties.Artist()) + "\n" + QString::number(sample.duration, 'f', 1);
    sample.observedAt = MediaMonitor::monotonicSeconds();
    // Publish a connected sample only after every required COM read succeeds.
    sample.found = true;
    return sample;
}
QString currentError() {
    try { throw; }
    catch (const hresult_error& e) { return qt(e.message()) + QStringLiteral(" (0x%1)").arg(static_cast<quint32>(e.code().value), 8, 16, QLatin1Char('0')); }
    catch (const std::exception& e) { return QString::fromUtf8(e.what()); }
}
} // namespace

SmtcReader::SmtcReader(ManagerRequest request)
    : request_(request ? std::move(request) : ManagerRequest([] { return Manager::RequestAsync(); })) {}

SmtcPoll SmtcReader::poll(const QString& selected, quint64 generation, std::stop_token stop) {
    SmtcPoll result;
    result.sample.id = selected;
    result.sample.generation = generation;
    result.sample.observedAt = MediaMonitor::monotonicSeconds();
    if (stop.stop_requested()) { result.cancelled = true; return result; }
    Windows::Foundation::Collections::IVectorView<GlobalSystemMediaTransportControlsSession> sessions{nullptr};
    try {
        if (!manager_) {
            if (std::chrono::steady_clock::now() < reconnectAt_) return result;
            auto request = request_();
            if (!waitFor(request, stop)) { result.cancelled = true; return result; }
            manager_ = request.GetResults();
            require(manager_, "SMTC returned a null session manager");
            cycle_ = 0;
        }
        sessions = manager_.GetSessions();
        require(sessions, "SMTC returned a null session collection");
        result.publishSessions = cycle_++ % 10 == 0;
        // Collection enumeration can also fail if the provider disappears mid-read.
        const auto count = sessions.Size();
        for (uint32_t index = 0; index < count; ++index) {
            if (stop.stop_requested()) { result.cancelled = true; return result; }
            QString id;
            QString title;
            try {
                const auto session = sessions.GetAt(index);
                require(session, "SMTC returned a null session");
                id = qt(session.SourceAppUserModelId());
                if (!result.publishSessions && id != selected) continue;
                auto request = session.TryGetMediaPropertiesAsync();
                if (!waitFor(request, stop)) { result.cancelled = true; return result; }
                const auto properties = request.GetResults();
                require(properties, "SMTC returned null media properties");
                title = qt(properties.Title());
                if (id == selected && !selected.isEmpty()) result.sample = readSample(session, properties, result.sample);
            } catch (const hresult_error&) {
                if (result.error.isEmpty()) result.error = id + ": " + currentError();
            } catch (const std::exception&) {
                if (result.error.isEmpty()) result.error = id + ": " + currentError();
            }
            // A failed provider keeps its ID available for selection/retry.
            if (result.publishSessions && !id.isEmpty()) result.sessions.push_back(QVariantMap{
                {"id", id}, {"title", title}, {"label", title.isEmpty() ? id : id + " — " + title}});
        }
    } catch (const hresult_error&) {
        result.error = currentError();
        manager_ = nullptr;
    } catch (const std::exception&) {
        result.error = currentError();
        manager_ = nullptr;
    }
    if (!manager_) {
        reconnectAt_ = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        result.publishSessions = true;
        result.sessions.clear();
        result.sample = MediaSample{};
        result.sample.id = selected;
        result.sample.generation = generation;
        result.sample.observedAt = MediaMonitor::monotonicSeconds();
    }
    result.cancelled = stop.stop_requested();
    return result;
}
