#include "platform/windows/PotPlayerBackend.h"
#include "platform/windows/PotPlayerLauncher.h"
#include "platform/windows/MediaMonitor.h"
#include "infrastructure/ExternalPlayer.h"
#include <QFileInfo>
#include <QUrl>
#include <windows.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <optional>
#include <cmath>

namespace {
constexpr WPARAM GetDuration = 0x5002, GetPosition = 0x5004, SetPosition = 0x5005,
    GetStatus = 0x5006, SetStatus = 0x5007;
struct Instance {
    HANDLE process{};
    DWORD pid{};
    quint64 created{};
    QString id, executable, video;
    std::atomic_bool closing{};
    HWND window{}; // Worker-thread confined, never shared with QML.
    bool invalidContent{}, announcedExit{}, closePosted{}, closeWarned{}, contentSeen{};
    double closeAt{}, knownDuration{}, rate{1};
    std::deque<std::pair<double, double>> positions;
    ~Instance() { if (process) CloseHandle(process); }
    bool alive() const { return process && WaitForSingleObject(process, 0) == WAIT_TIMEOUT; }
    HWND findWindow() const {
        struct Search { DWORD pid; HWND result{}; } search{pid};
        EnumWindows([](HWND hwnd, LPARAM data) -> BOOL {
            auto& search = *reinterpret_cast<Search*>(data);
            DWORD owner{}; GetWindowThreadProcessId(hwnd, &owner);
            wchar_t name[64]{}; GetClassNameW(hwnd, name, 64);
            if (owner == search.pid && QString::fromWCharArray(name) == "PotPlayer64" && !GetWindow(hwnd, GW_OWNER)) {
                search.result = hwnd; return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search));
        return search.result;
    }
    bool validWindow() const {
        DWORD owner{}; GetWindowThreadProcessId(window, &owner);
        wchar_t name[64]{}; GetClassNameW(window, name, 64);
        return alive() && IsWindow(window) && owner == pid && QString::fromWCharArray(name) == "PotPlayer64";
    }
    std::optional<qint64> message(WPARAM command, LPARAM value = 0) const {
        if (!validWindow()) return {};
        DWORD_PTR reply{};
        if (!SendMessageTimeoutW(window, WM_USER, command, value,
                SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT, 100, &reply)) return {};
        return qint64(static_cast<INT_PTR>(reply));
    }
};
// PotPlayer's native caption can temporarily contain an OSD message. Only a
// recognizable local media filename is evidence of an external file switch.
QString captionFile(HWND window) {
    wchar_t text[2048]{}; GetWindowTextW(window, text, 2048);
    auto title = QString::fromWCharArray(text).trimmed();
    if (title.endsWith(" - PotPlayer", Qt::CaseInsensitive)) title.chop(12);
    static const QStringList extensions{"mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "ts", "m2ts"};
    return extensions.contains(QFileInfo(title).suffix().toLower()) ? title : QString();
}
}
struct PotPlayerBackend::Impl {
    struct Command { MediaSample expected; bool play{}; quint64 request{}; double seconds{-1}; bool seek{}; };
    PotPlayerBackend* owner;
    std::mutex mutex;
    std::condition_variable wake;
    std::shared_ptr<Instance> current;
    std::deque<std::shared_ptr<Instance>> retired;
    std::deque<Command> commands;
    QString selected;
    quint64 generation{};
    std::jthread worker;
    explicit Impl(PotPlayerBackend* owner) : owner(owner), worker([this](std::stop_token stop) { run(stop); }) {}
    ~Impl() {
        { std::lock_guard lock(mutex); if (current) current->closing = true; }
        worker.request_stop(); wake.notify_all(); if (worker.joinable()) worker.join();
    }
    void close(const std::shared_ptr<Instance>& instance) {
        if (!instance->alive()) return;
        if (instance->closeAt == 0) instance->closeAt = MediaMonitor::monotonicSeconds();
        if (!instance->window || !instance->validWindow()) instance->window = instance->findWindow();
        if (!instance->closePosted && instance->validWindow()) {
            instance->closePosted = PostMessageW(instance->window, WM_CLOSE, 0, 0);
        }
        if (!instance->closeWarned && MediaMonitor::monotonicSeconds() - instance->closeAt > 5) {
            instance->closeWarned = true;
            emit owner->closeFailed(QStringLiteral("PotPlayer（PID %1）尚未退出，请在播放器中完成关闭；未强制终止进程").arg(instance->pid));
        }
    }
    MediaSample read(const std::shared_ptr<Instance>& instance, const QString& id, quint64 revision) {
        MediaSample sample; sample.id = id; sample.generation = revision;
        sample.sessionToken = instance->created;
        sample.observedAt = MediaMonitor::monotonicSeconds();
        sample.playback = instance->alive() ? MediaSample::Playback::Changing : MediaSample::Playback::Closed;
        if (!instance->alive() || instance->closing || instance->invalidContent) return sample;
        if (!instance->window) instance->window = instance->findWindow();
        // A replaced window is a new binding, not permission to reuse a stale HWND.
        if (!instance->validWindow()) return sample;
        const auto status = instance->message(GetStatus), duration = instance->message(GetDuration), position = instance->message(GetPosition);
        if (!status || !duration || !position) return sample;
        if (*status != 1 && *status != 2) { instance->positions.clear(); return sample; }
        if (*duration <= 0 || *duration > 366LL * 86400000 || *position < 0 || *position > *duration + 1000) return sample;
        const auto filename = QFileInfo(instance->video).fileName();
        const auto caption = captionFile(instance->window);
        if (!caption.isEmpty()) {
            const QFileInfo content(caption);
            const auto actual = content.isAbsolute() ? content.canonicalFilePath() : caption;
            const auto expected = content.isAbsolute() ? instance->video : filename;
            if (actual.compare(expected, Qt::CaseInsensitive) != 0) instance->invalidContent = true;
        }
        if (instance->knownDuration > 0 && std::abs(instance->knownDuration - *duration / 1000.0) > 0.5) instance->invalidContent = true;
        if (instance->invalidContent) {
            emit owner->errorOccurred(QStringLiteral("PotPlayer 内容已切换，已停止本次弹幕同步，请从应用重新打开视频"));
            // Deliver the identity change once so MediaSession stops checkpointing
            // the old file, in addition to freezing presentation immediately.
            sample.found = true; sample.sessionToken = instance->created + 1;
            sample.title = caption; sample.identity = instance->id + ":changed";
            return sample;
        }
        if (!caption.isEmpty()) instance->contentSeen = true;
        if (!instance->contentSeen) return sample;
        instance->knownDuration = *duration / 1000.0;
        sample.observedAt = MediaMonitor::monotonicSeconds();
        sample.position = *position / 1000.0; sample.duration = instance->knownDuration;
        sample.playing = *status == 2;
        sample.playback = sample.playing ? MediaSample::Playback::Playing : MediaSample::Playback::Paused;
        // The native protocol has no verified rate getter. Estimate interpolation
        // from a bounded history of actual media positions; never assume 1x.
        auto& history = instance->positions;
        if (!sample.playing || (!history.empty() && (sample.position < history.back().second
                || sample.position - history.back().second > (sample.observedAt - history.back().first) * 16 + 0.5))) history.clear();
        if (sample.playing) {
            history.emplace_back(sample.observedAt, sample.position);
            while (history.size() > 12) history.pop_front();
            if (history.size() >= 5) {
                const double elapsed = history.back().first - history.front().first;
                const double measured = (history.back().second - history.front().second) / elapsed;
                if (elapsed >= 0.4 && std::isfinite(measured) && measured >= 0.1 && measured <= 16) instance->rate = measured;
            }
        }
        sample.rate = instance->rate; sample.found = sample.canSeek = sample.canPlay = sample.canPause = true;
        sample.title = filename; sample.mediaUri = QUrl::fromLocalFile(instance->video).toString(QUrl::FullyEncoded);
        sample.identity = instance->id + ':' + sample.mediaUri;
        return sample;
    }
    void run(std::stop_token stop) {
        while (!stop.stop_requested()) {
            std::shared_ptr<Instance> instance; QString id; quint64 revision{};
            std::deque<std::shared_ptr<Instance>> closing;
            std::optional<Command> command;
            {
                std::lock_guard lock(mutex);
                instance = current; id = selected; revision = generation;
                closing = retired;
                if (!commands.empty()) { command = std::move(commands.front()); commands.pop_front(); }
            }
            if (instance) closing.push_back(instance);
            for (const auto& item : closing) {
                if (item->closing) close(item);
                if (!item->alive() && !item->announcedExit) { item->announcedExit = true; emit owner->ownedExited(item->id); }
            }
            { std::lock_guard lock(mutex); std::erase_if(retired, [](const auto& value) { return !value->alive(); }); }
            MediaSample sample;
            if (instance && id == instance->id) { sample = read(instance, id, revision); emit owner->sampleReceived(sample); }
            if (command) {
                const auto& expected = command->expected;
                const double age = MediaMonitor::monotonicSeconds() - expected.observedAt;
                bool currentSelection;
                { std::lock_guard lock(mutex); currentSelection = selected == id && generation == revision && current == instance; }
                bool okay = currentSelection && instance && !instance->closing && sample.found && expected.found
                    && expected.id == id && expected.generation == revision && expected.sessionToken == sample.sessionToken
                    && expected.mediaUri == sample.mediaUri && expected.title == sample.title && std::isfinite(age) && age >= 0 && age <= 2.5;
                if (okay) {
                    okay = instance->message(command->seek ? SetPosition : SetStatus,
                        command->seek ? LPARAM(std::llround(command->seconds * 1000)) : command->play ? 2 : 1).has_value();
                    instance->positions.clear();
                }
                const auto error = okay ? QString() : QStringLiteral("PotPlayer 媒体或连接已改变，或消息请求超时");
                if (command->seek) emit owner->seekFinished(command->request, okay, error);
                else emit owner->playbackControlFinished(command->request, okay, error);
            }
            std::unique_lock lock(mutex);
            if ((!current || !current->alive()) && retired.empty())
                wake.wait(lock, [&] { return stop.stop_requested() || (current && current->alive()) || !retired.empty(); });
            else wake.wait_for(lock, std::chrono::milliseconds(100));
        }
        // Shutdown can race the player's first window creation. Retain verified
        // identities briefly so cancelling immediately after launch still posts
        // a normal close to the eventual owned window. Never terminate a process.
        std::deque<std::shared_ptr<Instance>> shutdown;
        { std::lock_guard lock(mutex); shutdown = retired; if (current) shutdown.push_back(current); }
        const auto limit = MediaMonitor::monotonicSeconds() + 5;
        while (true) {
            bool waiting = false;
            for (const auto& instance : shutdown) {
                close(instance);
                waiting |= instance->alive() && !instance->closePosted;
            }
            if (!waiting || MediaMonitor::monotonicSeconds() >= limit) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    void enqueue(Command command) {
        bool accepted;
        { std::lock_guard lock(mutex);
            accepted = current && !current->closing && current->alive() && command.expected.id == selected
                && selected == current->id && command.expected.generation == generation && commands.size() < 8;
            if (accepted) commands.push_back(command);
        }
        if (accepted) wake.notify_all();
        else if (command.seek) emit owner->seekFinished(command.request, false, QStringLiteral("播放器连接已改变或控制队列已满"));
        else emit owner->playbackControlFinished(command.request, false, QStringLiteral("播放器连接已改变或控制队列已满"));
    }
};
PotPlayerBackend::PotPlayerBackend(QObject* parent) : MediaPlaybackBackend(parent), impl_(std::make_unique<Impl>(this)) {}
PotPlayerBackend::~PotPlayerBackend() = default;
ManagedPlaybackOpen PotPlayerBackend::open(const QString& executable, const QString& video) {
    auto issue = PotPlayerLauncher::validate(executable);
    if (!issue.isEmpty()) return {{}, issue};
    if (!QFileInfo(video).isAbsolute() || !QFileInfo(video).isFile()) return {{}, QStringLiteral("视频文件不存在")};
    { std::lock_guard lock(impl_->mutex); if (impl_->retired.size() >= 4) return {{}, QStringLiteral("旧播放器仍在退出，请先完成关闭")}; }
    qint64 pid{}; issue = ExternalPlayerProcess::launch(executable, {video, "/new"}, &pid);
    if (!issue.isEmpty()) return {{}, issue};
    auto instance = std::make_shared<Instance>(); instance->pid = DWORD(pid);
    instance->process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, instance->pid);
    if (!instance->process) return {{}, QStringLiteral("无法保留新播放器进程身份，请检查权限")};
    FILETIME created{}, exit{}, kernel{}, user{}; wchar_t path[32768]{}; DWORD size = 32768;
    if (!GetProcessTimes(instance->process, &created, &exit, &kernel, &user)
        || !QueryFullProcessImageNameW(instance->process, 0, path, &size)
        || QFileInfo(QString::fromWCharArray(path, size)).canonicalFilePath().compare(QFileInfo(executable).canonicalFilePath(), Qt::CaseInsensitive) != 0)
        return {{}, QStringLiteral("无法核实新播放器的程序路径或创建时间")};
    instance->created = (quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    instance->id = QString("danmax-potplayer:%1:%2").arg(pid).arg(instance->created);
    instance->executable = QFileInfo(executable).canonicalFilePath(); instance->video = QFileInfo(video).canonicalFilePath();
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->current) { impl_->current->closing = true; impl_->retired.push_back(impl_->current); }
        impl_->current = instance; impl_->commands.clear();
    }
    impl_->wake.notify_all(); return {instance->id, {}};
}
void PotPlayerBackend::closeOwned(const QString& id) {
    { std::lock_guard lock(impl_->mutex); if (impl_->current && (id.isEmpty() || impl_->current->id == id)) {
        impl_->current->closing = true; impl_->commands.clear();
    } }
    impl_->wake.notify_all();
}
void PotPlayerBackend::select(const QString& id, quint64 generation) {
    { std::lock_guard lock(impl_->mutex); impl_->selected = id; impl_->generation = generation; }
    impl_->wake.notify_all();
}
void PotPlayerBackend::requestPlayback(MediaSample expected, bool play, quint64 request) {
    impl_->enqueue({std::move(expected), play, request});
}
void PotPlayerBackend::requestSeek(MediaSample expected, double seconds, quint64 request) {
    if (!expected.canSeek || !std::isfinite(seconds) || seconds < 0 || !std::isfinite(expected.duration)
        || expected.duration <= 0 || expected.duration > 366 * 86400.0 || seconds > expected.duration) {
        emit seekFinished(request, false, QStringLiteral("定位目标无效")); return;
    }
    impl_->enqueue({std::move(expected), false, request, seconds, true});
}
