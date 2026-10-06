#include "platform/windows/MediaMonitor.h"
#include <windows.h>
#include <QFileInfo>
#include <algorithm>
#include <appmodel.h>
#include <chrono>
#include <psapi.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

using namespace winrt;
using namespace Windows::Media::Control;
namespace {
QString qt(const hstring& text) {
    return QString::fromWCharArray(text.c_str(), static_cast<qsizetype>(text.size()));
}
template <class Operation> bool waitFor(Operation& op, std::stop_token stop) {
    for (int i = 0; i < 60; ++i) {
        if (stop.stop_requested()) {
            op.Cancel();
            return false;
        }
        if (op.Status() != Windows::Foundation::AsyncStatus::Started)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    op.Cancel();
    throw std::runtime_error("Windows media query timed out");
}
} // namespace
MediaMonitor::MediaMonitor(QObject* parent) : QObject(parent) {
    qRegisterMetaType<MediaSample>();
    worker_ = std::jthread([this](std::stop_token s) { run(s); });
}
MediaMonitor::~MediaMonitor() {
    worker_.request_stop();
    if (worker_.joinable())
        worker_.join();
}
void MediaMonitor::select(QString id) {
    std::lock_guard lock(mutex_);
    selected_ = std::move(id);
}
void MediaMonitor::run(std::stop_token stop) {
    bool initialized = false;
    QString lastError;
    int cycle = 0;
    GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
    try {
        init_apartment(apartment_type::multi_threaded);
        initialized = true;
        auto managerRequest = GlobalSystemMediaTransportControlsSessionManager::RequestAsync();
        if (!waitFor(managerRequest, stop)) {
            uninit_apartment();
            return;
        }
        manager = managerRequest.GetResults();
        while (!stop.stop_requested()) {
            try {
                QString selected;
                {
                    std::lock_guard lock(mutex_);
                    selected = selected_;
                }
                MediaSample sample;
                QVariantList list;
                const auto sessions = manager.GetSessions();
                const bool listDue = cycle++ % 10 == 0;
                for (const auto& session : sessions) {
                    const auto id = qt(session.SourceAppUserModelId());
                    if (!listDue && id != selected)
                        continue;
                    auto request = session.TryGetMediaPropertiesAsync();
                    if (!waitFor(request, stop))
                        break;
                    const auto properties = request.GetResults();
                    const auto title = qt(properties.Title());
                    if (listDue)
                        list.push_back(QVariantMap{{"id", id},
                                                   {"title", title},
                                                   {"label", title.isEmpty() ? id : id + " — " + title}});
                    if (id != selected)
                        continue;
                    auto timeline = session.GetTimelineProperties();
                    auto playback = session.GetPlaybackInfo();
                    sample.id = id;
                    sample.title = title;
                    sample.found = true;
                    sample.duration =
                        std::chrono::duration<double>(timeline.EndTime() - timeline.StartTime()).count();
                    sample.position =
                        std::chrono::duration<double>(timeline.Position() - timeline.StartTime()).count();
                    sample.playing = playback.PlaybackStatus() ==
                                     GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
                    if (auto rate = playback.PlaybackRate())
                        sample.rate = std::clamp(rate.Value(), 0.0, 8.0);
                    const double age =
                        std::chrono::duration<double>(clock::now() - timeline.LastUpdatedTime()).count();
                    if (sample.playing && age > 0 && age < 86400)
                        sample.position += age * sample.rate;
                    sample.position = std::max(0.0, sample.position);
                    if (sample.duration > 0)
                        sample.position = std::min(sample.position, sample.duration);
                    sample.identity = title + "\n" + qt(properties.Artist()) + "\n" +
                                      QString::number(sample.duration, 'f', 1);
                }
                if (stop.stop_requested())
                    break;
                if (listDue)
                    emit sessionsChanged(list);
                emit sampleReceived(sample);
                lastError.clear();
            } catch (const hresult_error& e) {
                const auto msg = qt(e.message());
                if (msg != lastError) {
                    emit errorOccurred(msg);
                    lastError = msg;
                }
                emit sampleReceived({});
            } catch (const std::exception& e) {
                const auto msg = QString::fromUtf8(e.what());
                if (msg != lastError) {
                    emit errorOccurred(msg);
                    lastError = msg;
                }
                emit sampleReceived({});
            }
            for (int i = 0; i < 4 && !stop.stop_requested(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    } catch (const hresult_error& e) {
        emit errorOccurred(qt(e.message()));
    } catch (const std::exception& e) {
        emit errorOccurred(QString::fromUtf8(e.what()));
    }
    manager = nullptr;
    if (initialized)
        uninit_apartment();
}
bool MediaMonitor::targetForeground(const QString& id) {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    if (pid == GetCurrentProcessId())
        return true;
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return false;
    wchar_t path[32768];
    DWORD size = 32768;
    QString executable;
    if (QueryFullProcessImageNameW(process, 0, path, &size))
        executable = QFileInfo(QString::fromWCharArray(path, static_cast<int>(size))).fileName();
    UINT32 length = 0;
    QString aumid;
    if (GetApplicationUserModelId(process, &length, nullptr) == ERROR_INSUFFICIENT_BUFFER) {
        std::wstring buffer(length, L'\0');
        if (GetApplicationUserModelId(process, &length, buffer.data()) == ERROR_SUCCESS)
            aumid = QString::fromWCharArray(buffer.c_str());
    }
    CloseHandle(process);
    return (!aumid.isEmpty() && aumid == id) ||
           (!executable.isEmpty() &&
            (executable.compare(id, Qt::CaseInsensitive) == 0 ||
             QFileInfo(executable).completeBaseName().compare(id, Qt::CaseInsensitive) == 0));
}
QVariantMap MediaMonitor::processMetrics() {
    PROCESS_MEMORY_COUNTERS mem{};
    GetProcessMemoryInfo(GetCurrentProcess(), &mem, sizeof(mem));
    FILETIME created{}, exited{}, kernel{}, user{};
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    auto value = [](FILETIME t) {
        return (static_cast<unsigned long long>(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    };
    const auto cpu = value(kernel) + value(user);
    static auto previous = cpu;
    static auto stamp = std::chrono::steady_clock::now();
    const auto now = std::chrono::steady_clock::now();
    const double seconds = std::chrono::duration<double>(now - stamp).count();
    const double percent = seconds > 0 ? (cpu - previous) * 1e-7 / seconds * 100 /
                                             std::max(1u, std::thread::hardware_concurrency())
                                       : 0;
    previous = cpu;
    stamp = now;
    return {{"memoryMiB", static_cast<double>(mem.WorkingSetSize) / 1048576}, {"cpuPercent", percent}};
}

void MediaMonitor::maintainTopmost(quintptr handle, int strategy) {
    const auto hwnd = reinterpret_cast<HWND>(handle);
    constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;
    if (strategy == 3)
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
    if (strategy > 1)
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, flags);
}
QVariantMap MediaMonitor::windowMetrics(quintptr handle) {
    const auto hwnd = reinterpret_cast<HWND>(handle);
    const auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    return {{"nativeTransparentInput", bool(style & WS_EX_TRANSPARENT)},
            {"nativeNoActivate", bool(style & WS_EX_NOACTIVATE)},
            {"nativeTopmost", bool(style & WS_EX_TOPMOST)},
            {"overlayHasFocus", GetForegroundWindow() == hwnd}};
}
