#include "infrastructure/LibVlcBackend.h"
#include "platform/windows/MediaMonitor.h"
#include <vlc/vlc.h>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibrary>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QDebug>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <windows.h>

namespace {
struct Api {
    std::unique_ptr<QLibrary> library;
#define VLC_API(X) X(libvlc_new) X(libvlc_release) X(libvlc_free) X(libvlc_log_set) X(libvlc_log_unset) \
    X(libvlc_media_new_path) X(libvlc_media_release) X(libvlc_media_get_mrl) X(libvlc_media_get_stats) \
    X(libvlc_media_player_new) X(libvlc_media_player_release) X(libvlc_media_player_set_media) \
    X(libvlc_media_player_get_media) X(libvlc_media_player_set_hwnd) X(libvlc_video_set_key_input) X(libvlc_video_set_mouse_input) \
    X(libvlc_media_player_play) X(libvlc_media_player_stop) X(libvlc_media_player_set_pause) \
    X(libvlc_media_player_get_state) X(libvlc_media_player_get_time) X(libvlc_media_player_get_length) \
    X(libvlc_media_player_set_time) X(libvlc_media_player_is_seekable) X(libvlc_video_get_size)
#define DECLARE(name) decltype(&::name) name{};
    VLC_API(DECLARE)
#undef DECLARE
    DLL_DIRECTORY_COOKIE dllDirectory{};
    DLL_DIRECTORY_COOKIE qtDirectory{};
    void load() {
        if (library && library->isLoaded()) return;
        if (!library) library = std::make_unique<QLibrary>();
        if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS)) throw std::runtime_error("Cannot restrict DLL search");
        wchar_t qtPath[32768]{};
        auto qt = GetModuleHandleW(L"Qt6Core.dll");
        if (!qt) qt = GetModuleHandleW(L"Qt6Cored.dll");
        if (qt && GetModuleFileNameW(qt, qtPath, 32768)) {
            const auto directory = QDir::toNativeSeparators(QFileInfo(QString::fromWCharArray(qtPath)).absolutePath());
            if (!qtDirectory) qtDirectory = AddDllDirectory(reinterpret_cast<LPCWSTR>(directory.utf16()));
        }
        // Absolute loading plus an explicitly registered dependency directory.
        if (!dllDirectory) dllDirectory = AddDllDirectory(reinterpret_cast<LPCWSTR>(QDir::toNativeSeparators(LibVlcBackend::runtimeDirectory()).utf16()));
        if (!dllDirectory) throw std::runtime_error("Cannot register libVLC DLL directory");
        library->setFileName(QDir(LibVlcBackend::runtimeDirectory()).filePath("libvlc.dll"));
        if (!library->load()) throw std::runtime_error(library->errorString().toStdString());
#define RESOLVE(name) name = reinterpret_cast<decltype(name)>(library->resolve(#name)); if (!name) throw std::runtime_error("Missing " #name);
        VLC_API(RESOLVE)
#undef RESOLVE
    }
    ~Api() { if (library) library->unload(); if (dllDirectory) RemoveDllDirectory(dllDirectory); if (qtDirectory) RemoveDllDirectory(qtDirectory); }
};
std::atomic<quint64> tokens{1};
}

class LibVlcWorker final : public QObject {
    Q_OBJECT
public:
    Api api;
    libvlc_instance_t* instance{};
    libvlc_media_player_t* player{};
    libvlc_media_t* media{};
    QTimer* timer{};
    QString id, content, launch, selected;
    quint64 token{}, generation{};
    MediaSample latest;
    // Callback stores bounded diagnostics only; Qt logging happens on poll/release.
    std::mutex logMutex;
    QStringList logLines;
    static void log(void* opaque, int level, const libvlc_log_t*, const char* format, va_list args) {
        auto* self = static_cast<LibVlcWorker*>(opaque);
        char line[2048]{}; vsnprintf(line, sizeof(line), format, args);
        std::lock_guard lock(self->logMutex);
        if (self->logLines.size() < 128) self->logLines.append(QStringLiteral("libVLC[%1] %2").arg(level).arg(QString::fromUtf8(line)));
    }
    void drainLogs() {
        QStringList lines;
        { std::lock_guard lock(logMutex); lines.swap(logLines); }
        for (const auto& line : lines) qInfo().noquote() << line;
    }
    void release() {
        if (timer) timer->stop();
        const auto old = id;
        if (player) { api.libvlc_media_player_stop(player); api.libvlc_media_player_release(player); player = nullptr; }
        if (media) { api.libvlc_media_release(media); media = nullptr; }
        if (instance) { api.libvlc_log_unset(instance); api.libvlc_release(instance); instance = nullptr; }
        id.clear(); latest = {}; drainLogs();
        if (!old.isEmpty()) emit released(old);
    }
    void open(const QString& session, const QString& file, quintptr hwnd) {
        release(); id = session; token = ++tokens; content = QUuid::createUuid().toString(QUuid::Id128);
        launch = QUrl::fromLocalFile(file).toString(QUrl::FullyEncoded);
        try {
            api.load();
            const char* args[]{"--ignore-config", "--no-media-library", "--no-metadata-network-access", "--no-lua",
                "--no-video-title-show", "--no-osd", "--no-plugins-cache", "--avcodec-hw=any", "--vout=direct3d11", "--verbose=1"};
            instance = api.libvlc_new(int(std::size(args)), args);
            if (!instance) throw std::runtime_error("libvlc_new failed");
            api.libvlc_log_set(instance, log, this);
            player = api.libvlc_media_player_new(instance);
            media = api.libvlc_media_new_path(instance, QDir::toNativeSeparators(file).toUtf8().constData());
            if (!player || !media) throw std::runtime_error("Cannot create libVLC media/player");
            api.libvlc_media_player_set_media(player, media);
            api.libvlc_media_player_set_hwnd(player, reinterpret_cast<void*>(hwnd));
            api.libvlc_video_set_key_input(player, 0); api.libvlc_video_set_mouse_input(player, 0);
            if (api.libvlc_media_player_play(player) != 0) throw std::runtime_error("libVLC rejected play");
            if (!timer) { timer = new QTimer(this); timer->setInterval(100); connect(timer, &QTimer::timeout, this, &LibVlcWorker::poll); }
            timer->start();
        } catch (const std::exception& error) { emit failed(session, QString::fromUtf8(error.what())); release(); }
    }
    void poll() {
        if (!player || selected != id) return;
        const auto observedAt = MediaMonitor::monotonicSeconds();
        const auto state = api.libvlc_media_player_get_state(player);
        MediaSample sample;
        sample.id = id; sample.generation = generation; sample.sessionToken = token; sample.contentId = content;
        sample.launchUri = launch; sample.connection = MediaSample::Connection::Alive; sample.found = true;
        auto* current = api.libvlc_media_player_get_media(player);
        if (current) {
            if (auto* mrl = api.libvlc_media_get_mrl(current)) { sample.mediaUri = QString::fromUtf8(mrl); api.libvlc_free(mrl); }
            api.libvlc_media_release(current);
        }
        sample.evidence = sample.mediaUri.isEmpty() ? MediaSample::Evidence::LaunchTarget : MediaSample::Evidence::Verified;
        sample.title = QFileInfo(QUrl(sample.mediaUri).toLocalFile()).fileName();
        sample.duration = std::max(0.0, api.libvlc_media_player_get_length(player) / 1000.0);
        sample.position = std::max(0.0, api.libvlc_media_player_get_time(player) / 1000.0);
        sample.rate = 1; // v1 exposes no speed control.
        sample.playing = state == libvlc_Playing;
        sample.playback = state == libvlc_Playing ? MediaSample::Playback::Playing : state == libvlc_Paused ? MediaSample::Playback::Paused
            : state == libvlc_Ended || state == libvlc_Stopped ? MediaSample::Playback::Stopped : MediaSample::Playback::Changing;
        if (state == libvlc_Ended && latest.duration > 0) { sample.duration = latest.duration; sample.position = latest.duration; }
        if (state == libvlc_Error) sample.connection = MediaSample::Connection::Unavailable;
        sample.canPlay = sample.canPause = sample.playback == MediaSample::Playback::Playing || sample.playback == MediaSample::Playback::Paused;
        sample.canSeek = sample.canPlay && api.libvlc_media_player_is_seekable(player);
        sample.seekPrecision = MediaSample::SeekPrecision::Unspecified;
        sample.observedAt = observedAt; latest = sample;
        unsigned width{}, height{}; api.libvlc_video_get_size(player, 0, &width, &height);
        libvlc_media_stats_t stats{}; api.libvlc_media_get_stats(media, &stats);
        emit observed(sample, {{"videoWidth", int(width)}, {"videoHeight", int(height)}, {"displayedPictures", stats.i_displayed_pictures},
            {"lostPictures", stats.i_lost_pictures}, {"playedAudioBuffers", stats.i_played_abuffers}});
        drainLogs();
    }
    bool valid(const MediaSample& expected) {
        poll();
        const double age = MediaMonitor::monotonicSeconds() - expected.observedAt;
        return player && selected == id && latest.sameConnection(expected) && !latest.contentDiffers(expected)
            && latest.usableTimeline() && expected.usableTimeline() && age >= 0 && age <= 2.5;
    }
    void playback(MediaSample expected, bool play, quint64 request) {
        if (!valid(expected)) { emit controlled(request, false, QStringLiteral("内置播放器连接、内容或采样已失效")); return; }
        api.libvlc_media_player_set_pause(player, play ? 0 : 1);
        emit controlled(request, true, {}); poll();
    }
    void seek(MediaSample expected, double seconds, quint64 request) {
        if (!valid(expected) || !latest.canSeek || !std::isfinite(seconds) || seconds < 0 || seconds > latest.duration) {
            emit dispatched({request, SeekDispatch::Outcome::Rejected, {}, 0, 0, QStringLiteral("内置播放器定位条件已失效")}); return;
        }
        const auto before = latest; const auto issued = MediaMonitor::monotonicSeconds();
        api.libvlc_media_player_set_time(player, libvlc_time_t(seconds * 1000));
        emit dispatched({request, SeekDispatch::Outcome::Submitted, before, issued, MediaMonitor::monotonicSeconds(), {}});
        poll();
    }
signals:
    void observed(MediaSample sample, QVariantMap metrics);
    void controlled(quint64 request, bool success, QString error);
    void dispatched(SeekDispatch dispatch);
    void failed(QString id, QString error);
    void released(QString id);
};

QString LibVlcBackend::runtimeDirectory() { return QDir(QCoreApplication::applicationDirPath()).filePath("libvlc"); }
QString LibVlcBackend::preflight() {
    const QDir directory(runtimeDirectory());
    for (const auto* file : {"libvlc.dll", "libvlccore.dll", "plugins"})
        if (!QFileInfo::exists(directory.filePath(file))) return QStringLiteral("内置播放器运行库不完整，请重新准备开发运行目录：%1").arg(directory.path());
    return {};
}
LibVlcBackend::LibVlcBackend(QObject* parent) : MediaPlaybackBackend(parent), worker_(new LibVlcWorker) {
    worker_->moveToThread(&thread_);
    connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
    connect(worker_, &LibVlcWorker::observed, this, [this](MediaSample sample, QVariantMap metrics) {
        if (sample.id != current_ || closing_.contains(sample.id)) return;
        metrics_ = std::move(metrics); emit sampleReceived(sample);
    });
    connect(worker_, &LibVlcWorker::controlled, this, &MediaPlaybackBackend::playbackControlFinished);
    connect(worker_, &LibVlcWorker::dispatched, this, [this](SeekDispatch result) { emit seekDispatched(result); emit seekFinished(result.request, result.outcome == SeekDispatch::Outcome::Submitted, result.error); });
    connect(worker_, &LibVlcWorker::failed, this, [this](const QString& id, const QString& error) { if (id == current_) emit errorOccurred(error); });
    connect(worker_, &LibVlcWorker::released, this, [this](const QString& id) {
        resources_.remove(id); closing_.remove(id); if (id == current_) current_.clear();
        emit activityChanged(); emit released(id);
    });
    thread_.start();
}
LibVlcBackend::~LibVlcBackend() {
    // Product close is asynchronous. A C++ owner must also keep the output HWND
    // alive until the queued release is complete; never terminate a native call.
    QMetaObject::invokeMethod(worker_, [worker = worker_] { worker->release(); QThread::currentThread()->quit(); });
    thread_.wait();
}
ManagedPlaybackOpen LibVlcBackend::open(const QString&, const QString& video) {
    const auto error = preflight(); if (!error.isEmpty()) return {{}, error};
    const QFileInfo file(video);
    if (!file.isAbsolute() || !file.isFile()) return {{}, QStringLiteral("内置播放器需要有效的本地视频文件")};
    if (!handle_ || !IsWindow(reinterpret_cast<HWND>(handle_))) return {{}, QStringLiteral("内置视频视口尚未准备完成")};
    const auto id = "danmax-libvlc:" + QUuid::createUuid().toString(QUuid::Id128);
    ++*intent_; current_ = id; resources_.insert(id); emit activityChanged();
    QMetaObject::invokeMethod(worker_, [worker = worker_, id, video, hwnd = handle_] { worker->open(id, video, hwnd); });
    return {id, {}};
}
void LibVlcBackend::select(const QString& id, quint64 generation) {
    ++*intent_;
    QMetaObject::invokeMethod(worker_, [worker = worker_, id, generation] { worker->selected = id; worker->generation = generation; worker->poll(); });
}
void LibVlcBackend::cancelOpen() { closeOwned(current_); }
void LibVlcBackend::closeOwned(const QString& id) {
    if (id.isEmpty() || !resources_.contains(id) || closing_.contains(id)) return;
    if (id == current_) ++*intent_;
    closing_.insert(id); emit activityChanged();
    QMetaObject::invokeMethod(worker_, [worker = worker_, id] { if (worker->id == id) worker->release(); });
    QTimer::singleShot(8000, this, [this, id] { if (resources_.contains(id)) emit closeDelayed(QStringLiteral("内置播放器仍在等待释放；未强制终止线程")); });
}
void LibVlcBackend::requestPlayback(MediaSample expected, bool play, quint64 request) {
    const auto intent = intent_->load();
    QMetaObject::invokeMethod(worker_, [worker = worker_, expected, play, request, guard = intent_, intent] {
        if (guard->load() != intent) { emit worker->controlled(request, false, QStringLiteral("播放任务已改变")); return; }
        worker->playback(expected, play, request);
    });
}
void LibVlcBackend::requestSeek(MediaSample expected, double seconds, quint64 request) {
    const auto intent = intent_->load();
    QMetaObject::invokeMethod(worker_, [worker = worker_, expected, seconds, request, guard = intent_, intent] {
        if (guard->load() != intent) { emit worker->dispatched({request, SeekDispatch::Outcome::Rejected, {}, 0, 0, QStringLiteral("定位任务已改变")}); return; }
        worker->seek(expected, seconds, request);
    });
}
#include "LibVlcBackend.moc"
