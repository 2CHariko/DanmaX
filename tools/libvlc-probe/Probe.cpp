#include "core/DanmakuEngine.h"
#include "renderer/DanmakuItem.h"
#include <vlc/libvlc.h>
#include <vlc/libvlc_media.h>
#include <vlc/libvlc_renderer_discoverer.h>
#include <vlc/libvlc_media_player.h>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QBackingStore>
#include <QPainter>
#include <QExposeEvent>
#include <QResizeEvent>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QScreen>
#include <QThread>
#include <QTimer>
#include <QLibrary>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QOperatingSystemVersion>
#include <QSysInfo>
#include <QPointer>
#include <QUrl>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <windows.h>
#include <psapi.h>

// A plain unpainted QWindow leaves letterbox regions undefined. Give the native
// host an opaque backing store; VLC owns and paints its video child above it.
class NativeVideoWindow final : public QWindow {
    QBackingStore backing{this};
    void paintBlack() {
        if (!isExposed() || size().isEmpty()) return;
        backing.resize(size());
        const QRect region(QPoint(0, 0), size());
        backing.beginPaint(region);
        { QPainter painter(backing.paintDevice()); painter.fillRect(region, Qt::black); }
        backing.endPaint(); backing.flush(region);
    }
    void exposeEvent(QExposeEvent*) override { paintBlack(); }
    void resizeEvent(QResizeEvent*) override { paintBlack(); }
};

// C ABI only. Do not import Qt UI plugins from the VLC installation.
struct VlcApi {
    QLibrary library;
#define VLC_FUNCTIONS(X) \
    X(libvlc_new) X(libvlc_release) X(libvlc_get_version) X(libvlc_errmsg) X(libvlc_log_set) X(libvlc_log_unset) \
    X(libvlc_media_new_path) X(libvlc_media_release) X(libvlc_media_get_stats) \
    X(libvlc_media_player_new) X(libvlc_media_player_release) X(libvlc_media_player_set_media) \
    X(libvlc_media_player_set_hwnd) X(libvlc_video_set_key_input) X(libvlc_video_set_mouse_input) \
    X(libvlc_media_player_play) X(libvlc_media_player_stop) X(libvlc_media_player_set_pause) \
    X(libvlc_media_player_get_state) X(libvlc_media_player_get_time) X(libvlc_media_player_get_length) \
    X(libvlc_media_player_set_time) X(libvlc_media_player_get_rate) X(libvlc_media_player_is_seekable) \
    X(libvlc_video_get_size) X(libvlc_audio_set_volume) X(libvlc_audio_get_track_count)
#define DECLARE(name) decltype(&::name) name{};
    VLC_FUNCTIONS(DECLARE)
#undef DECLARE
    void load(const QString& path) {
        library.setFileName(path);
        if (!library.load()) throw std::runtime_error(library.errorString().toStdString());
#define RESOLVE(name) name = reinterpret_cast<decltype(name)>(library.resolve(#name)); if (!name) throw std::runtime_error("Missing " #name);
        VLC_FUNCTIONS(RESOLVE)
#undef RESOLVE
    }
};

class VlcWorker final : public QObject {
    Q_OBJECT
public:
    VlcApi api;
    libvlc_instance_t* instance{};
    libvlc_media_player_t* player{};
    libvlc_media_t* media{};
    QTimer* timer{};
    QElapsedTimer clock;
    std::mutex logMutex;
    QStringList logs;
    static void log(void* opaque, int level, const libvlc_log_t*, const char* format, va_list args) {
        auto* self = static_cast<VlcWorker*>(opaque);
        char message[2048]{};
        vsnprintf(message, sizeof(message), format, args);
        std::lock_guard guard(self->logMutex);
        if (self->logs.size() < 2000) self->logs.append(QString::number(level) + ": " + QString::fromUtf8(message));
    }
    void start(const QString& library, const QString& file, quintptr hwnd) {
        try {
            api.load(library);
            const char* args[]{"--ignore-config", "--no-media-library", "--no-metadata-network-access", "--no-lua",
                "--no-video-title-show", "--no-osd", "--no-plugins-cache", "--avcodec-hw=any", "--vout=direct3d11", "--verbose=2"};
            instance = api.libvlc_new(int(std::size(args)), args);
            if (!instance) throw std::runtime_error("libvlc_new failed");
            api.libvlc_log_set(instance, log, this);
            player = api.libvlc_media_player_new(instance);
            if (!player) throw std::runtime_error(std::string("Cannot create libVLC player: ") + (api.libvlc_errmsg() ? api.libvlc_errmsg() : "no error"));
            // VLC 3 Windows path2uri requires native drive separators; Qt's
            // canonicalFilePath uses '/', which VLC rejects as drive-relative.
            media = api.libvlc_media_new_path(instance, QDir::toNativeSeparators(file).toUtf8().constData());
            if (!media) throw std::runtime_error(std::string("Cannot create libVLC media for ") + file.toStdString() + ": " + (api.libvlc_errmsg() ? api.libvlc_errmsg() : "no error"));
            api.libvlc_media_player_set_media(player, media);
            api.libvlc_media_player_set_hwnd(player, reinterpret_cast<void*>(hwnd));
            api.libvlc_video_set_key_input(player, 0);
            api.libvlc_video_set_mouse_input(player, 0);
            api.libvlc_audio_set_volume(player, 20);
            clock.start();
            if (api.libvlc_media_player_play(player) != 0) throw std::runtime_error("libvlc play rejected");
            timer = new QTimer(this);
            timer->setInterval(100);
            connect(timer, &QTimer::timeout, this, &VlcWorker::poll);
            timer->start();
        } catch (const std::exception& e) { emit failed(QString::fromUtf8(e.what())); }
    }
    void poll() {
        if (!player) return;
        const int state = api.libvlc_media_player_get_state(player);
        unsigned width{}, height{};
        api.libvlc_video_get_size(player, 0, &width, &height);
        libvlc_media_stats_t stats{};
        api.libvlc_media_get_stats(media, &stats);
        QJsonObject data{{"state", state}, {"time", api.libvlc_media_player_get_time(player) / 1000.0},
            {"duration", api.libvlc_media_player_get_length(player) / 1000.0}, {"rate", api.libvlc_media_player_get_rate(player)},
            {"seekable", bool(api.libvlc_media_player_is_seekable(player))}, {"videoWidth", int(width)}, {"videoHeight", int(height)},
            {"decodedVideo", stats.i_decoded_video}, {"displayedPictures", stats.i_displayed_pictures}, {"lostPictures", stats.i_lost_pictures},
            {"playedAudioBuffers", stats.i_played_abuffers}, {"audioTracks", api.libvlc_audio_get_track_count(player)},
            {"version", QString::fromUtf8(api.libvlc_get_version())}, {"workerElapsedMs", double(clock.elapsed())}};
        emit sample(data);
    }
    void pause(bool value) { if (player) api.libvlc_media_player_set_pause(player, value ? 1 : 0); }
    void seek(double value) { if (player) api.libvlc_media_player_set_time(player, libvlc_time_t(value * 1000)); }
    void shutdown() {
        QElapsedTimer elapsed; elapsed.start();
        if (timer) timer->stop();
        if (player) { api.libvlc_media_player_stop(player); api.libvlc_media_player_release(player); player = nullptr; }
        if (media) { api.libvlc_media_release(media); media = nullptr; }
        if (instance) { api.libvlc_log_unset(instance); api.libvlc_release(instance); instance = nullptr; }
        QStringList output;
        { std::lock_guard guard(logMutex); output = logs; }
        emit stopped(elapsed.elapsed(), output);
    }
signals:
    void sample(QJsonObject data);
    void failed(QString error);
    void stopped(qint64 ms, QStringList logs);
};

class Probe final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QWindow* videoWindow READ videoWindow CONSTANT)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool paused READ paused NOTIFY changed)
    Q_PROPERTY(double position READ position NOTIFY changed)
    Q_PROPERTY(double duration READ duration NOTIFY changed)
public:
    NativeVideoWindow video;
    QPointer<QQuickWindow> host, layer;
    QPointer<DanmakuItem> canvas;
    QThread thread;
    VlcWorker* worker{};
    danmaku::Engine engine;
    QTimer frame, checks, watchdog;
    QElapsedTimer wall, tickClock, stepClock, sampleClock;
    QString directory, library, mediaFile, message{"正在初始化"};
    QJsonObject latest, report;
    QJsonArray assertions, samples, captures;
    int step{}, inputChanges{}, presentationFrames{};
    std::atomic<int> renderedFrames{};
    bool keepOpen{}, closing{}, stopped{}, autoDone{}, resetPending{};
    double time{}, lastRaw{-1}, length{}, rate{1}, anchor{}, pauseTime{}, pauseX{}, target{}, seekTarget{};
    int state{}, pauseCount{};
    std::vector<double> tickIntervals;
    Probe(QString dll, QString file, QString output, bool keep) : directory(std::move(output)), library(std::move(dll)), mediaFile(std::move(file)), keepOpen(keep) {
        video.setTitle("DanmaX libVLC probe native video");
        video.setFlags(Qt::FramelessWindowHint);
        video.create();
        const auto hwnd = reinterpret_cast<HWND>(video.winId());
        SetWindowLongPtrW(hwnd, GWL_STYLE, GetWindowLongPtrW(hwnd, GWL_STYLE) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
        worker = new VlcWorker;
        worker->moveToThread(&thread);
        connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
        connect(worker, &VlcWorker::failed, this, [this](const QString& error) { assertion("libvlc-start", false, error); close(); });
        connect(worker, &VlcWorker::sample, this, &Probe::observe);
        connect(worker, &VlcWorker::stopped, this, [this](qint64 ms, const QStringList& logs) {
            stopped = true;
            report["shutdownCompleted"] = true; report["shutdownMs"] = double(ms);
            QFile output(directory + "/vlc.log");
            if (output.open(QIODevice::WriteOnly)) output.write(logs.join('\n').toUtf8());
            QStringList hardware;
            for (const auto& line : logs) if (line.contains("D3D11", Qt::CaseInsensitive) || line.contains("hardware", Qt::CaseInsensitive) || line.contains("adapter", Qt::CaseInsensitive)) hardware.append(line);
            report["hardwareLogEvidence"] = QJsonArray::fromStringList(hardware);
            write(); thread.quit(); QGuiApplication::exit(passed() ? 0 : 2);
        });
        thread.start(); wall.start(); tickClock.start();
        frame.setTimerType(Qt::PreciseTimer); frame.setInterval(16);
        connect(&frame, &QTimer::timeout, this, &Probe::tick);
        checks.setInterval(100); connect(&checks, &QTimer::timeout, this, &Probe::advance);
        watchdog.setSingleShot(true); watchdog.setInterval(45000);
        connect(&watchdog, &QTimer::timeout, this, [this] { assertion("suite-deadline", false, message); close(); });
        report = {{"scope", "isolated libVLC HWND + owned Qt Quick top-level overlay probe; not product backend acceptance"},
            {"qt", qVersion()}, {"buildType", "Release"}, {"video", mediaFile}, {"library", library},
            {"os", QSysInfo::prettyProductName()}, {"cpuArchitecture", QSysInfo::currentCpuArchitecture()},
            {"frameCallbackUsed", false}, {"shutdownCompleted", false},
            {"speakerAudibility", "not verified; audio buffers and tracks are recorded separately"}};
    }
    ~Probe() override {
        frame.stop(); checks.stop();
        if (!stopped) QMetaObject::invokeMethod(worker, &VlcWorker::shutdown, Qt::BlockingQueuedConnection);
        thread.quit(); thread.wait();
    }
    QWindow* videoWindow() { return &video; }
    QString status() const { return message; }
    bool paused() const { return state == libvlc_Paused; }
    double position() const { return time; }
    double duration() const { return length; }
    void alignLayer() {
        if (!host || !layer || !canvas) return;
        layer->setGeometry(QRect(video.mapToGlobal(QPoint(0, 0)), video.size()));
        layer->setVisible(host->isVisible() && host->visibility() != QWindow::Minimized);
    }
    void assertion(const QString& name, bool okay, const QString& detail = {}) {
        assertions.append(QJsonObject{{"name", name}, {"passed", okay}, {"detail", detail}});
        message = name + (okay ? " · 通过" : " · 失败") + (detail.isEmpty() ? "" : " · " + detail);
        emit changed(); write();
    }
    bool passed() const {
        if (!autoDone) return false;
        for (const auto& item : assertions) if (!item.toObject()["passed"].toBool()) return false;
        return true;
    }
    Q_INVOKABLE void attach(QObject* item, QObject* window, QObject* main) {
        canvas = qobject_cast<DanmakuItem*>(item); layer = qobject_cast<QQuickWindow*>(window); host = qobject_cast<QQuickWindow*>(main);
        if (!canvas || !layer || !host) throw std::runtime_error("Probe QML objects missing");
        canvas->configure({{"fontFamily", "Microsoft YaHei UI"}, {"fontSize", 28}, {"strokeWidth", 1}, {"lineSpacing", .2}, {"opacity", 1.0}});
        std::vector<danmaku::Item> items;
        for (int i = 0; i < 2000; ++i) items.push_back({i * .8, i % 13 == 0 ? danmaku::Mode::Top : danmaku::Mode::Scroll,
            QString("libVLC 原生画面 · Qt Quick 弹幕 %1").arg(i + 1).toStdString(), i % 3 ? 0xffffffU : 0x50e0ffU});
        engine.load(std::move(items));
        const auto resize = [this] { danmaku::Options o; o.trackHeight = canvas->trackHeight(); o.maxTracks = 40;
            engine.reconfigure(o, canvas->width(), canvas->height(), [this](const auto& entry) { return canvas->measure(entry); }); };
        connect(canvas, &QQuickItem::widthChanged, this, resize); connect(canvas, &QQuickItem::heightChanged, this, resize);
        for (auto* surface : {static_cast<QWindow*>(host.data()), static_cast<QWindow*>(&video)}) {
            connect(surface, &QWindow::xChanged, this, &Probe::alignLayer);
            connect(surface, &QWindow::yChanged, this, &Probe::alignLayer);
            connect(surface, &QWindow::widthChanged, this, &Probe::alignLayer);
            connect(surface, &QWindow::heightChanged, this, &Probe::alignLayer);
            connect(surface, &QWindow::visibleChanged, this, &Probe::alignLayer);
            connect(surface, &QWindow::visibilityChanged, this, &Probe::alignLayer);
        }
        connect(layer, &QQuickWindow::frameSwapped, this, [this] { renderedFrames.fetch_add(1); }, Qt::DirectConnection);
        QTimer::singleShot(400, this, [this, resize] {
            alignLayer(); resize();
            if (host->screen()) report["screen"] = QJsonObject{{"name", host->screen()->name()}, {"refreshHz", host->screen()->refreshRate()},
                {"width", host->screen()->size().width()}, {"height", host->screen()->size().height()}, {"dpr", host->devicePixelRatio()}};
            const auto handle = quintptr(video.winId());
            QMetaObject::invokeMethod(worker, [this, handle] { worker->start(library, mediaFile, handle); });
            frame.start(); checks.start(); watchdog.start(); stepClock.start(); host->requestActivate();
        });
    }
    void observe(const QJsonObject& data) {
        const double raw = data["time"].toDouble();
        const int next = data["state"].toInt();
        if (raw != lastRaw || next != state || resetPending) {
            ++inputChanges; time = std::max(0.0, raw); anchor = wall.elapsed() / 1000.0; lastRaw = raw;
        }
        state = next; rate = data["rate"].toDouble(1); length = data["duration"].toDouble(); latest = data;
        if (resetPending && std::abs(raw - seekTarget) < .5) { engine.seek(raw); resetPending = false; }
        if (samples.size() < 1800) { auto entry = data; entry["wallMs"] = double(wall.elapsed()); samples.append(entry); }
        sampleClock.restart(); emit changed();
        if (state == libvlc_Error) { assertion("decoder-state", false, "libVLC Error"); close(); }
    }
    void tick() {
        if (!canvas || !layer || closing) return;
        const double dt = tickClock.nsecsElapsed() / 1e9; tickClock.restart();
        if (tickIntervals.size() < 20000) tickIntervals.push_back(dt * 1000);
        const bool playing = state == libvlc_Playing && sampleClock.isValid() && sampleClock.elapsed() < 1000;
        double position = lastRaw < 0 ? 0 : std::max(0.0, lastRaw);
        if (playing) position += std::clamp(wall.elapsed() / 1000.0 - anchor, 0.0, .5) * rate;
        if (resetPending) { time = seekTarget; return; }
        time = position;
        engine.tick(position, dt * rate, playing, [this](const auto& item) { return canvas->measure(item); });
        canvas->present(engine); emit changed();
    }
    Q_INVOKABLE void togglePause() { const bool pause = state != libvlc_Paused; QMetaObject::invokeMethod(worker, [this, pause] { worker->pause(pause); }); }
    Q_INVOKABLE void seek(double seconds) {
        if (!std::isfinite(seconds) || !latest["seekable"].toBool()) return;
        seconds = std::clamp(seconds, 0.0, std::max(0.0, length - .2));
        engine.seek(seconds); if (canvas) canvas->present(engine);
        resetPending = true; seekTarget = seconds;
        QMetaObject::invokeMethod(worker, [this, seconds] { worker->seek(seconds); });
    }
    Q_INVOKABLE void capture(const QString& name) {
        if (!host || !host->screen()) return;
        const auto foregroundWindow = GetForegroundWindow();
        const auto hostHandle = reinterpret_cast<HWND>(host->winId());
        const bool foreground = foregroundWindow == hostHandle || GetAncestor(foregroundWindow, GA_ROOTOWNER) == hostHandle;
        const auto origin = host->mapToGlobal(QPoint(0, 0));
        const auto image = host->screen()->grabWindow(0, origin.x(), origin.y(), host->width(), host->height());
        const QString path = directory + "/" + name + ".png";
        const bool saved = !image.isNull() && image.save(path);
        captures.append(QJsonObject{{"name", name}, {"foreground", foreground}, {"saved", saved}, {"path", path},
            {"hostWidth", host->width()}, {"hostHeight", host->height()}, {"dpr", host->devicePixelRatio()},
            {"viewportWidth", canvas->width()}, {"viewportHeight", canvas->height()}});
        write();
    }
    void next() { ++step; stepClock.restart(); }
    void advance() {
        if (closing || autoDone || !canvas) return;
        const auto ms = stepClock.elapsed();
        switch (step) {
        case 0:
            if (state == libvlc_Playing && lastRaw > 1 && latest["displayedPictures"].toInt() > 3 && canvas->sceneEntries() > 0) {
                assertion("native-video-and-existing-renderer", true); capture("playing");
                QMetaObject::invokeMethod(worker, [this] { worker->pause(true); }); next();
            } else if (ms > 12000) { assertion("native-video-and-existing-renderer", false, "No progressing video/rendered comments before deadline"); close(); }
            break;
        case 1:
            if (state == libvlc_Paused && ms > 900) {
                pauseTime = lastRaw; pauseX = canvas->firstSnapshotX(); pauseCount = canvas->snapshotCount(); next();
            } else if (ms > 5000) { assertion("pause-readback", false); close(); }
            break;
        case 2:
            if (ms > 900) {
                assertion("pause-freezes-media-comments", state == libvlc_Paused && std::abs(lastRaw - pauseTime) < .001
                    && std::abs(canvas->firstSnapshotX() - pauseX) < .001 && pauseCount == canvas->snapshotCount(),
                    QString("media delta %1 s; comment delta %2 px").arg(lastRaw - pauseTime).arg(canvas->firstSnapshotX() - pauseX));
                capture("paused"); target = length > 80 ? 60.25 : std::min(20.25, length / 2); seek(target);
                assertion("seek-clears-active-comments", engine.activeCount() == 0); next();
            }
            break;
        case 3:
            if (ms > 600 && state == libvlc_Paused && std::abs(lastRaw - target) < .5) {
                assertion("paused-seek-position-readback", true, QString("target %1; actual %2 s; probe tolerance 0.5 s, not frame accuracy").arg(target).arg(lastRaw));
                QMetaObject::invokeMethod(worker, [this] { worker->pause(false); }); next();
            } else if (ms > 8000) { assertion("paused-seek-position-readback", false, QString::number(lastRaw)); close(); }
            break;
        case 4:
            if (ms > 1800 && state == libvlc_Playing && lastRaw > target + .5 && canvas->sceneEntries() > 0) {
                assertion("resume-after-seek", true); host->resize(880, 620); next();
            } else if (ms > 7000) { assertion("resume-after-seek", false); close(); }
            break;
        case 5:
            if (ms > 1200) {
                assertion("resize-native-layer-alignment", video.size() == layer->size() && std::abs(canvas->width() - video.width()) < 1
                    && video.parent() == host && layer->transientParent() == host
                    && layer->position() == video.mapToGlobal(QPoint(0, 0)));
                capture("resized"); host->showFullScreen(); next();
            }
            break;
        case 6:
            if (ms > 1500) {
                assertion("fullscreen-native-layer-alignment", host->visibility() == QWindow::FullScreen && video.size() == layer->size());
                capture("fullscreen"); QMetaObject::invokeMethod(host, "showTestPopup"); next();
            }
            break;
        case 7:
            if (ms > 1200) { capture("fullscreen-popup"); host->showNormal(); host->resize(1120, 760); next(); }
            break;
        case 8:
            if (ms > 1200) {
                assertion("return-from-fullscreen", host->visibility() == QWindow::Windowed && video.size() == layer->size());
                autoDone = true; watchdog.stop(); checks.stop(); message = "自动检查完成；Space 暂停，F11 全屏，可检查浮层";
                report["automatedSuiteComplete"] = true;
                assertion("qml-warning-free", qEnvironmentVariableIntValue("DANMAX_PROBE_QML_WARNINGS") == 0);
                message = "自动检查完成；Space 暂停，F11 全屏，可检查浮层"; write(); emit changed();
                if (!keepOpen) close();
            }
            break;
        }
    }
    void write() {
        report["passed"] = passed(); report["assertions"] = assertions; report["samples"] = samples; report["captures"] = captures;
        report["lastMediaSample"] = latest; report["elapsedMs"] = double(wall.elapsed());
        report["activeComments"] = double(engine.activeCount()); report["imageNodes"] = canvas ? canvas->imageNodeCount() : 0;
        report["overlayPresentedFrames"] = renderedFrames.load(); report["inputPositionChanges"] = inputChanges;
        report["qmlWarnings"] = qEnvironmentVariableIntValue("DANMAX_PROBE_QML_WARNINGS");
        if (!tickIntervals.empty()) { auto sorted = tickIntervals; std::sort(sorted.begin(), sorted.end());
            report["guiTickP95Ms"] = sorted[std::min(sorted.size() - 1, std::size_t(sorted.size() * .95))]; }
        PROCESS_MEMORY_COUNTERS memory{}; GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory));
        report["workingSetMiB"] = double(memory.WorkingSetSize) / 1048576;
        report["visualComposition"] = "Requires separate foreground visual review; automatic state/geometry checks do not establish composition correctness";
        QSaveFile file(directory + "/result.json");
        if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(report).toJson()); file.commit(); }
    }
    Q_INVOKABLE void close() {
        if (closing) return;
        closing = true; frame.stop(); checks.stop(); watchdog.stop(); message = "正在停止并释放 libVLC…"; emit changed();
        if (!autoDone) assertion("suite-completed-before-close", false, "Probe closed before automatic checks completed");
        QMetaObject::invokeMethod(worker, &VlcWorker::shutdown);
        QTimer::singleShot(8000, this, [this] {
            if (!stopped) { report["shutdownExceeded8Seconds"] = true; message = "libVLC 尚未释放，仍在等待；未强制终止线程"; write(); emit changed(); }
        });
    }
signals:
    void changed();
};

int main(int argc, char** argv) {
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("DanmaX libVLC probe");
    QCommandLineParser parser; parser.addHelpOption();
    parser.addOption(QCommandLineOption("library", "Absolute project-local libvlc.dll", "path"));
    parser.addOption(QCommandLineOption("video", "Local video", "path"));
    parser.addOption(QCommandLineOption("report", "Evidence directory", "path"));
    parser.addOption(QCommandLineOption("keep-open", "Keep window open after automated checks"));
    parser.process(app);
    const auto dll = QFileInfo(parser.value("library")).canonicalFilePath();
    const auto media = QFileInfo(parser.value("video")).canonicalFilePath();
    const auto output = QFileInfo(parser.value("report")).absoluteFilePath();
    if (dll.isEmpty() || media.isEmpty() || !QDir().mkpath(output)) return 1;
    const auto vlcDirectory = QFileInfo(dll).absolutePath();
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    // Qt style plugins load after startup; retain the already loaded, explicit
    // Qt runtime directory without restoring implicit PATH DLL searches.
    wchar_t qtModulePath[32768]{};
    GetModuleFileNameW(GetModuleHandleW(L"Qt6Core.dll"), qtModulePath, DWORD(std::size(qtModulePath)));
    const auto qtDirectory = QFileInfo(QString::fromWCharArray(qtModulePath)).absolutePath();
    const auto qtCookie = AddDllDirectory(reinterpret_cast<const wchar_t*>(qtDirectory.utf16()));
    const auto cookie = AddDllDirectory(reinterpret_cast<const wchar_t*>(vlcDirectory.utf16()));
    qputenv("VLC_PLUGIN_PATH", QDir(vlcDirectory).filePath("plugins").toUtf8());
    int code = 1;
    {
        Probe probe(dll, media, output, parser.isSet("keep-open"));
        qmlRegisterType<DanmakuItem>("DanmaX.Probe", 1, 0, "DanmakuCanvas");
        QQmlApplicationEngine engine;
        QObject::connect(&engine, &QQmlApplicationEngine::warnings, &app, [&](const QList<QQmlError>& errors) {
            qputenv("DANMAX_PROBE_QML_WARNINGS", QByteArray::number(qEnvironmentVariableIntValue("DANMAX_PROBE_QML_WARNINGS") + errors.size()));
            for (const auto& error : errors) fprintf(stderr, "%s\n", qPrintable(error.toString()));
        });
        engine.setInitialProperties({{"probe", QVariant::fromValue(&probe)}});
        engine.load(QUrl("qrc:/Probe.qml"));
        if (engine.rootObjects().isEmpty()) return 1;
        code = app.exec();
    }
    if (cookie) RemoveDllDirectory(cookie);
    if (qtCookie) RemoveDllDirectory(qtCookie);
    return code;
}
#include "Probe.moc"
