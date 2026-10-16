#include "platform/windows/MediaMonitor.h"
#include "platform/windows/SmtcReader.h"
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <propkey.h>
#include <QFileInfo>
#include <algorithm>
#include <optional>
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
quint64 MediaMonitor::select(QString id, bool renew) {
    std::lock_guard lock(mutex_);
    if (renew || id != selected_) { ++generation_; commands_.clear(); }
    selected_ = std::move(id);
    return generation_;
}
void MediaMonitor::requestPlayback(MediaSample expected, bool play, quint64 request) {
    std::lock_guard lock(mutex_);
    if (commands_.size() < 8) commands_.push_back({std::move(expected), play, request});
}
quint64 MediaMonitor::generation() {
    std::lock_guard lock(mutex_);
    return generation_;
}
double MediaMonitor::monotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
void MediaMonitor::run(std::stop_token stop) {
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
                if (error.isEmpty()) error = reader.control(command->expected, command->play, stop, [this, &command] {
                    std::lock_guard lock(mutex_);
                    return command->expected.generation == generation_ && command->expected.id == selected_;
                });
                emit playbackControlFinished(command->request, error.isEmpty(), error);
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
bool MediaMonitor::targetForeground(const QString& id) {
    const auto foreground = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    if (pid == GetCurrentProcessId())
        return true;
    return windowMatchesSession(reinterpret_cast<quintptr>(foreground), id);
}
bool MediaMonitor::windowMatchesSession(quintptr handle, const QString& id) {
    if (!handle || id.isEmpty())
        return false;
    const auto window = reinterpret_cast<HWND>(handle);
    // Packaged players can be hosted by ApplicationFrameHost, whose process has no player AUMID.
    // The window property store carries the identity of the actual hosted application.
    winrt::com_ptr<IPropertyStore> properties;
    if (SUCCEEDED(SHGetPropertyStoreForWindow(window, IID_PPV_ARGS(properties.put())))) {
        PROPVARIANT value{};
        const HRESULT result = properties->GetValue(PKEY_AppUserModel_ID, &value);
        const QString windowId = SUCCEEDED(result) && value.vt == VT_LPWSTR && value.pwszVal
                                     ? QString::fromWCharArray(value.pwszVal)
                                     : QString{};
        PropVariantClear(&value);
        if (windowId == id)
            return true;
        if (!windowId.isEmpty() && id.contains(QLatin1Char('!')))
            return false;
    }
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
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

bool MediaMonitor::windowOrOwnerForeground(quintptr handle) {
    const auto main = reinterpret_cast<HWND>(handle);
    if (!main || !IsWindow(main)) return false;
    auto foreground = GetForegroundWindow();
    if (foreground) foreground = GetAncestor(foreground, GA_ROOT);
    // Native file dialogs may have no corresponding QWindow. Follow HWND owners.
    for (int depth = 0; foreground && depth < 64; ++depth) {
        if (foreground == main) return true;
        foreground = GetWindow(foreground, GW_OWNER);
    }
    return false;
}

QVariantMap MediaMonitor::windowMetrics(quintptr handle) {
    const auto hwnd = reinterpret_cast<HWND>(handle);
    const auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    return {{"nativeTransparentInput", bool(style & WS_EX_TRANSPARENT)},
            {"nativeNoActivate", bool(style & WS_EX_NOACTIVATE)},
            {"nativeTopmost", bool(style & WS_EX_TOPMOST)},
            {"overlayHasFocus", GetForegroundWindow() == hwnd}};
}
