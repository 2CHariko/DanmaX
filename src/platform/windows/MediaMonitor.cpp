#include "platform/windows/MediaMonitor.h"
#include "platform/windows/SmtcBackend.h"
#include "infrastructure/VlcHttpBackend.h"
#include "platform/windows/PlayerProcessIdentity.h"
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
MediaMonitor::MediaMonitor(QObject* parent, const QString& playerDirectory) : QObject(parent) {
    qRegisterMetaType<MediaSample>();
    smtc_ = new SmtcBackend(this); vlc_ = new VlcHttpBackend(playerDirectory, this); active_ = smtc_;
    for (auto* backend : {static_cast<MediaPlaybackBackend*>(smtc_), static_cast<MediaPlaybackBackend*>(vlc_)}) {
        connect(backend, &MediaPlaybackBackend::sampleReceived, this, [this, backend](MediaSample sample) {
            if (backend == active_ && sample.id == selected_ && sample.generation == generation_) emit sampleReceived(sample);
        });
        connect(backend, &MediaPlaybackBackend::errorOccurred, this, [this, backend](const QString& error) {
            if (backend == active_) emit errorOccurred(error);
        });
        connect(backend, &MediaPlaybackBackend::playbackControlFinished, this, &MediaMonitor::playbackControlFinished);
    }
    connect(smtc_, &MediaPlaybackBackend::sessionsChanged, this, [this](QVariantList list) { smtcSessions_ = std::move(list); publishSessions(); });
    connect(vlc_, &MediaPlaybackBackend::sessionsChanged, this, [this](QVariantList list) { vlcSessions_ = std::move(list); publishSessions(); });
    smtc_->select({}, generation_);
}
MediaMonitor::~MediaMonitor() {
    // Stop the worker while the facade's captured state still exists.
    delete smtc_; smtc_ = nullptr;
    delete vlc_; vlc_ = nullptr;
}
void MediaMonitor::publishSessions() { auto list = smtcSessions_; list.append(vlcSessions_); emit sessionsChanged(list); }
quint64 MediaMonitor::select(QString id, bool renew) {
    if (!renew && id == selected_) return generation_;
    ++generation_; selected_ = std::move(id);
    active_ = vlc_->accepts(selected_) ? static_cast<MediaPlaybackBackend*>(vlc_) : smtc_;
    smtc_->select(active_ == smtc_ ? selected_ : QString(), generation_);
    vlc_->select(active_ == vlc_ ? selected_ : QString(), generation_);
    return generation_;
}
void MediaMonitor::requestPlayback(MediaSample expected, bool play, quint64 request) {
    if (expected.generation != generation_ || expected.id != selected_) {
        emit playbackControlFinished(request, false, QStringLiteral("选择的播放器已改变")); return;
    }
    active_->requestPlayback(std::move(expected), play, request);
}
quint64 MediaMonitor::generation() { return generation_; }
ManagedPlaybackOpen MediaMonitor::openManaged(const QString& adapter, const QString& executable, const QString& video) {
    if (adapter != "vlc") return {{}, QStringLiteral("播放器没有可用的媒体后端")};
    return vlc_->open(executable, video);
}
void MediaMonitor::cancelManagedOpen() { vlc_->cancelOpen(); }
double MediaMonitor::monotonicSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
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
    if (id.startsWith("danmax-vlc:")) return PlayerProcessIdentity::matchesWindow(handle, id);
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
