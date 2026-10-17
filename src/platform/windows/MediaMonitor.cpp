#include "platform/windows/MediaMonitor.h"
#include "platform/windows/PotPlayerBackend.h"
#include "infrastructure/VlcHttpBackend.h"
#include "platform/windows/PlayerProcessIdentity.h"
#include <windows.h>
#include <QFileInfo>
#include <algorithm>
#include <optional>
#include <chrono>
#include <psapi.h>

MediaMonitor::MediaMonitor(QObject* parent, const QString& playerDirectory) : QObject(parent) {
    qRegisterMetaType<MediaSample>();
    vlc_ = new VlcHttpBackend(playerDirectory, this);
    pot_ = new PotPlayerBackend(this);
    connect(pot_, &PotPlayerBackend::ownedExited, this, &MediaMonitor::ownedPlayerExited);
    connect(pot_, &PotPlayerBackend::closeFailed, this, &MediaMonitor::ownedPlayerCloseFailed);
    for (auto* backend : {static_cast<MediaPlaybackBackend*>(vlc_), static_cast<MediaPlaybackBackend*>(pot_)}) {
        connect(backend, &MediaPlaybackBackend::sampleReceived, this, [this, backend](MediaSample sample) {
            if (backend == active_ && sample.id == selected_ && sample.generation == generation_) emit sampleReceived(sample);
        });
        connect(backend, &MediaPlaybackBackend::errorOccurred, this, [this, backend](const QString& error) {
            if (backend == active_) emit errorOccurred(error);
        });
        connect(backend, &MediaPlaybackBackend::playbackControlFinished, this, &MediaMonitor::playbackControlFinished);
        connect(backend, &MediaPlaybackBackend::seekFinished, this, &MediaMonitor::seekFinished);
    }
    connect(vlc_, &MediaPlaybackBackend::sessionsChanged, this, [this](QVariantList list) { vlcSessions_ = std::move(list); publishSessions(); });
}
MediaMonitor::~MediaMonitor() {
    // Stop the worker while the facade's captured state still exists.
    delete vlc_; vlc_ = nullptr;
    delete pot_; pot_ = nullptr;
}
void MediaMonitor::publishSessions() { auto list = vlcSessions_; emit sessionsChanged(list); }
quint64 MediaMonitor::select(QString id, bool renew) {
    if (!renew && id == selected_) return generation_;
    ++generation_; selected_ = std::move(id);
    active_ = pot_->accepts(selected_) ? static_cast<MediaPlaybackBackend*>(pot_)
        : vlc_->accepts(selected_) ? static_cast<MediaPlaybackBackend*>(vlc_) : nullptr;
    vlc_->select(active_ == vlc_ ? selected_ : QString(), generation_);
    pot_->select(active_ == pot_ ? selected_ : QString(), generation_);
    return generation_;
}
void MediaMonitor::requestPlayback(MediaSample expected, bool play, quint64 request) {
    if (!active_ || expected.generation != generation_ || expected.id != selected_) {
        emit playbackControlFinished(request, false, QStringLiteral("选择的播放器已改变")); return;
    }
    active_->requestPlayback(std::move(expected), play, request);
}
quint64 MediaMonitor::generation() { return generation_; }
void MediaMonitor::requestSeek(MediaSample expected, double seconds, quint64 request) {
    if (!active_ || expected.generation != generation_ || expected.id != selected_) {
        emit seekFinished(request, false, QStringLiteral("选择的播放器已改变")); return;
    }
    active_->requestSeek(std::move(expected), seconds, request);
}
ManagedPlaybackOpen MediaMonitor::openManaged(const QString& adapter, const QString& executable, const QString& video) {
    if (adapter == "potplayer") return pot_->open(executable, video);
    if (adapter != "vlc") return {{}, QStringLiteral("播放器没有可用的媒体后端")};
    return vlc_->open(executable, video);
}
void MediaMonitor::cancelManagedOpen() { vlc_->cancelOpen(); pot_->closeOwned(); }
void MediaMonitor::closeOwnedPlayer(const QString& session) {
    if (pot_->accepts(session)) pot_->closeOwned(session);
}
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
    return (id.startsWith("danmax-vlc:") || id.startsWith("danmax-potplayer:"))
        && PlayerProcessIdentity::matchesWindow(handle, id);
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
