#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QWindow>
#include <QProcess>
#include <QProcessEnvironment>
#include <QWinEventNotifier>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <QSaveFile>
#include <future>
#include <memory>
#include <windows.h>
#include <tlhelp32.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

namespace {
bool onlyExpectedProcesses(const QList<DWORD>& expected) {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry); bool okay = true;
    if (!Process32FirstW(snapshot, &entry)) okay = false;
    else do {
        const auto name = QString::fromWCharArray(entry.szExeFile);
        if ((name.compare("PotPlayerMini64.exe", Qt::CaseInsensitive) == 0 || name.compare("PotPlayerMini.exe", Qt::CaseInsensitive) == 0)
            && !expected.contains(entry.th32ProcessID)) okay = false;
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot); return okay;
}
QByteArray chunk(const char* type, const QByteArray& body) {
    QByteArray result(type, 4); QDataStream out(&result, QIODevice::Append); out.setByteOrder(QDataStream::LittleEndian);
    out << quint32(body.size()); result += body; if (body.size() % 2) result += '\0'; return result;
}
QByteArray list(const char* type, const QByteArray& body) { return chunk("LIST", QByteArray(type, 4) + body); }
void video(const QString& path, bool blue) {
    constexpr int width = 80, height = 48, frames = 300, size = width * height * 3;
    QByteArray aviHeader; QDataStream a(&aviHeader, QIODevice::WriteOnly); a.setByteOrder(QDataStream::LittleEndian);
    for (const quint32 value : {200000U, quint32(size * 5), 0U, 16U, quint32(frames), 0U, 1U, quint32(size), quint32(width), quint32(height), 0U, 0U, 0U, 0U}) a << value;
    QByteArray stream("vidsDIB ", 8); QDataStream s(&stream, QIODevice::Append); s.setByteOrder(QDataStream::LittleEndian);
    s << quint32(0) << quint16(0) << quint16(0);
    for (const quint32 value : {0U, 1U, 5U, 0U, quint32(frames), quint32(size), 0xffffffffU, 0U}) s << value;
    s << qint16(0) << qint16(0) << qint16(width) << qint16(height);
    QByteArray format; QDataStream f(&format, QIODevice::WriteOnly); f.setByteOrder(QDataStream::LittleEndian);
    f << quint32(40) << qint32(width) << qint32(height) << quint16(1) << quint16(24) << quint32(0) << quint32(size)
      << qint32(0) << qint32(0) << quint32(0) << quint32(0);
    QByteArray pixels(size, '\0'), movi, index;
    QDataStream idx(&index, QIODevice::WriteOnly); idx.setByteOrder(QDataStream::LittleEndian);
    for (int frame = 0; frame < frames; ++frame) {
        for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const int at = (y * width + x) * 3;
            pixels[at] = char(blue ? 170 : 40); pixels[at + 1] = char((x + frame) % width * 2 + 30); pixels[at + 2] = char(blue ? 40 : 170);
        }
        idx.writeRawData("00db", 4); idx << quint32(16) << quint32(movi.size() + 4) << quint32(size);
        movi += chunk("00db", pixels);
    }
    const auto hdrl = list("hdrl", chunk("avih", aviHeader) + list("strl", chunk("strh", stream) + chunk("strf", format)));
    const auto data = chunk("RIFF", QByteArray("AVI ", 4) + hdrl + list("movi", movi) + chunk("idx1", index));
    QFile file(path); if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) throw std::runtime_error("Cannot write probe AVI");
}
QJsonArray mediaSessions() {
    return std::async(std::launch::async, [] {
        QJsonArray result;
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            auto manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            for (auto session : manager.GetSessions()) {
                const auto id = QString::fromWCharArray(session.SourceAppUserModelId().c_str());
                if (!id.contains("PotPlayer", Qt::CaseInsensitive)) continue;
                auto properties = session.TryGetMediaPropertiesAsync().get();
                const auto timeline = session.GetTimelineProperties();
                result.append(QJsonObject{{"id", id}, {"title", QString::fromWCharArray(properties.Title().c_str())},
                    {"playback", int(session.GetPlaybackInfo().PlaybackStatus())},
                    {"positionSeconds", double(timeline.Position().count()) / 10000000.0}});
            }
        } catch (const winrt::hresult_error& error) { result.append(QJsonObject{{"error", QString::fromWCharArray(error.message().c_str())}}); }
        winrt::uninit_apartment(); return result;
    }).get();
}
struct Instance {
    HANDLE handle{}; DWORD pid{}; quint64 created{}; QString role;
    HWND bound{};
    ~Instance() { if (handle) CloseHandle(handle); }
    bool alive() const { return handle && WaitForSingleObject(handle, 0) == WAIT_TIMEOUT; }
    HWND window() const {
        struct Search { DWORD pid; HWND found; } search{pid, nullptr};
        EnumWindows([](HWND hwnd, LPARAM value) -> BOOL {
            auto& target = *reinterpret_cast<Search*>(value); DWORD process = 0; GetWindowThreadProcessId(hwnd, &process);
            if (process == target.pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
                wchar_t title[1024]; GetWindowTextW(hwnd, title, 1024);
                if (title[0]) { target.found = hwnd; return FALSE; }
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&search)); return search.found;
    }
    QString title() const { wchar_t title[1024]{}; GetWindowTextW(window(), title, 1024); return QString::fromWCharArray(title); }
    QString windowClass() const { wchar_t name[256]{}; GetClassNameW(window(), name, 256); return QString::fromWCharArray(name); }
    qint64 message(UINT type, WPARAM command, LPARAM argument = 0) const {
        DWORD owner = 0;
        if (bound) GetWindowThreadProcessId(bound, &owner);
        if (!alive() || !IsWindow(bound) || owner != pid) throw std::runtime_error("Rejected stale process/window binding");
        DWORD_PTR reply = 0; SetLastError(ERROR_SUCCESS);
        if (!SendMessageTimeoutW(bound, type, command, argument, SMTO_ABORTIFHUNG | SMTO_BLOCK | SMTO_ERRORONEXIT, 300, &reply))
            throw std::runtime_error((QString("PotPlayer IPC failed: %1").arg(GetLastError())).toStdString());
        return qint64(static_cast<INT_PTR>(reply));
    }
    bool close() const {
        if (!alive()) return true;
        const auto hwnd = window(); DWORD owner = 0; if (hwnd) GetWindowThreadProcessId(hwnd, &owner);
        return owner == pid && PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    QJsonObject info() const { return {{"pid", int(pid)}, {"createdFileTime", QString::number(created)}, {"role", role}, {"alive", alive()}, {"title", title()}, {"windowClass", windowClass()}}; }
};
}

class Probe final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString stage READ stage NOTIFY changed)
    Q_PROPERTY(bool layerVisible READ layerVisible NOTIFY changed)
public:
    Probe(QString executable, QString reportDirectory, bool ipc) : executable_(std::move(executable)), directory_(std::move(reportDirectory)), ipc_(ipc) {}
    QString stage() const { return stage_; }
    bool layerVisible() const { return layer_; }
    void start() {
        try {
            video(QDir(directory_).filePath("sentinel-A.avi"), false);
            video(QDir(directory_).filePath("owned-B.avi"), true);
            if (ipc_) {
                // Same file and title: SMTC's application ID/title cannot select an instance.
                sentinel_ = launch("sentinel", "owned-B.avi", "/new");
                owned_ = launch("owned", "owned-B.avi", "/new");
                check(sentinel_->pid != owned_->pid, "New instances reused a PID");
                after(3000, [this] { ipcStep(0); });
                QTimer::singleShot(60000, this, [this] { fail("IPC probe timed out"); });
                return;
            }
            sentinel_ = launch("sentinel", "sentinel-A.avi", "/new");
            after(2500, [this] { check(sentinel_->alive() && sentinel_->window(), "Sentinel failed to open");
                event("sentinel-ready", {{"sentinel", sentinel_->info()}});
                check(onlyExpectedProcesses({sentinel_->pid}), "An unrelated PotPlayer appeared; refusing /current test");
                forwarded_ = launch("current-launcher", "owned-B.avi", "/current");
                after(2500, [this] {
                    event("current-reuse", {{"launcher", forwarded_->info()}, {"sentinel", sentinel_->info()}, {"smtc", mediaSessions()}});
                    if (forwarded_->alive()) forwarded_->close();
                    openOwned("toolbar-close");
                });
            });
            QTimer::singleShot(120000, this, [this] { fail("Probe timed out waiting for test actions"); });
        } catch (const std::exception& e) { fail(e.what()); }
    }
    Q_INVOKABLE void endPlayback() {
        if (stage_ != "toolbar-close" || closing_) return;
        closing_ = true; closeAt_.start(); layer_ = false; emit changed();
        event("toolbar-end-request", {{"owned", owned_->info()}});
        if (!owned_->close()) fail("Cannot post normal close to owned HWND");
    }
signals:
    void changed();
private:
    // Queries/seek: ld3l/PotPlayerControl b4483d52; explicit state values:
    // https://www.cnblogs.com/hyaray/p/17050617.html (author's AHK implementation).
    // No third-party code/dependency is linked; compatibility is measured here.
    static constexpr WPARAM durationCommand = 0x5002, positionCommand = 0x5004,
        seekCommand = 0x5005, statusCommand = 0x5006, setStatusCommand = 0x5007;
    QJsonObject sample(const Instance& instance) const {
        return {{"pid", int(instance.pid)}, {"durationMs", instance.message(WM_USER, durationCommand)},
            {"positionMs", instance.message(WM_USER, positionCommand)}, {"status", instance.message(WM_USER, statusCommand)}};
    }
    void assertSentinel() {
        const auto state = sample(*sentinel_);
        check(state["status"].toInteger() == 1 && qAbs(state["positionMs"].toInteger() - 10000) <= 200,
            "Control leaked into sentinel instance");
    }
    void assertSeek(qint64 target) {
        const auto state = sample(*owned_);
        check(state["status"].toInteger() == 1 && qAbs(state["positionMs"].toInteger() - target) <= 200,
            "Paused seek readback disagrees with target");
        assertSentinel();
        event("seek-confirmed", {{"targetMs", target}, {"owned", state}, {"sentinel", sample(*sentinel_)}});
    }
    void ipcStep(int step) {
        int wait = 500;
        switch (step) {
        case 0:
            for (auto* instance : {sentinel_.get(), owned_.get()}) {
                check(instance->alive() && instance->windowClass() == "PotPlayer64", "Expected live PotPlayer64 main window");
                instance->bound = instance->window();
                const auto state = sample(*instance);
                check(state["durationMs"].toInteger() == 60000, "Duration is not 60000 milliseconds");
            }
            event("ipc-ready", {{"sentinel", sentinel_->info()}, {"owned", owned_->info()}, {"smtc", mediaSessions()}});
            sentinel_->message(WM_USER, setStatusCommand, 1);
            sentinel_->message(WM_USER, seekCommand, 10000);
            owned_->message(WM_USER, seekCommand, 2000);
            owned_->message(WM_USER, setStatusCommand, 2);
            break;
        case 1:
            event("command-readback", {{"owned", sample(*owned_)}, {"sentinel", sample(*sentinel_)}});
            assertSentinel(); initialPosition_ = owned_->message(WM_USER, positionCommand);
            check(owned_->message(WM_USER, statusCommand) == 2, "Play did not yield running status");
            event("playing-baseline", {{"owned", sample(*owned_)}}); wait = 1200; break;
        case 2:
            check(owned_->message(WM_USER, positionCommand) - initialPosition_ >= 600, "Running position did not advance");
            event("playing-advanced", {{"owned", sample(*owned_)}});
            owned_->message(WM_USER, setStatusCommand, 1); break;
        case 3:
            check(owned_->message(WM_USER, statusCommand) == 1, "Pause did not yield paused status");
            initialPosition_ = owned_->message(WM_USER, positionCommand);
            event("paused-baseline", {{"owned", sample(*owned_)}}); wait = 1200; break;
        case 4:
            check(qAbs(owned_->message(WM_USER, positionCommand) - initialPosition_) <= 200, "Paused position kept advancing");
            assertSentinel(); event("pause-frozen", {{"owned", sample(*owned_)}});
            owned_->message(WM_USER, seekCommand, 12500); break;
        case 5:
            assertSeek(12500); owned_->message(WM_USER, seekCommand, 40000); break;
        case 6:
            assertSeek(40000); stage_ = "ipc-inspect";
            event("ipc-inspect", {{"owned", sample(*owned_)}, {"sentinel", sample(*sentinel_)}});
            wait = 20000; break; // Brief stable interval for native UI inspection.
        case 7:
            assertSeek(40000); owned_->message(WM_USER, seekCommand, 5000); break;
        case 8:
            assertSeek(5000);
            owned_->message(WM_USER, setStatusCommand, 2); owned_->message(WM_USER, setStatusCommand, 2); break;
        case 9:
            check(owned_->message(WM_USER, statusCommand) == 2, "Repeated explicit play was not idempotent");
            initialPosition_ = owned_->message(WM_USER, positionCommand);
            event("resume-baseline", {{"owned", sample(*owned_)}}); wait = 1200; break;
        case 10:
            check(owned_->message(WM_USER, positionCommand) - initialPosition_ >= 600, "Resume did not advance position");
            event("resume-advanced", {{"owned", sample(*owned_)}});
            owned_->message(WM_USER, setStatusCommand, 1); owned_->message(WM_USER, setStatusCommand, 1); break;
        case 11:
            check(owned_->message(WM_USER, statusCommand) == 1, "Repeated explicit pause was not idempotent");
            assertSentinel(); event("isolated-control-passed", {{"owned", sample(*owned_)}, {"sentinel", sample(*sentinel_)}, {"smtc", mediaSessions()}});
            check(owned_->close(), "Owned instance refused normal close"); wait = 2000; break;
        case 12: {
            check(!owned_->alive(), "Owned instance did not exit"); assertSentinel();
            bool rejected = false;
            try { owned_->message(WM_USER, positionCommand); } catch (const std::exception&) { rejected = true; }
            check(rejected, "Exited process binding accepted a query");
            event("stale-binding-rejected", {{"owned", owned_->info()}, {"sentinel", sample(*sentinel_)}});
            check(sentinel_->close(), "Sentinel refused normal close"); wait = 2000; break;
        }
        case 13:
            check(!sentinel_->alive(), "Sentinel did not exit"); stage_ = "passed";
            event("passed", {{"mode", "pid-hwnd-ipc"}}); QCoreApplication::quit(); return;
        default: throw std::runtime_error("Unexpected IPC step");
        }
        after(wait, [this, step] { ipcStep(step + 1); });
    }
    template<class F> void after(int milliseconds, F operation) {
        QTimer::singleShot(milliseconds, this, [this, operation] { if (failed_) return; try { operation(); } catch (const std::exception& e) { fail(e.what()); } });
    }
    void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
    std::unique_ptr<Instance> launch(const QString& role, const QString& file, const QString& option) {
        QProcess process; process.setProgram(executable_); process.setArguments({QDir(directory_).filePath(file), option});
        process.setWorkingDirectory(QFileInfo(executable_).absolutePath());
        auto environment = QProcessEnvironment::systemEnvironment();
        for (const auto* name : {"QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QT_QPA_PLATFORM", "QT_QUICK_BACKEND", "QT_SCALE_FACTOR", "QML_DISK_CACHE_PATH"}) environment.remove(name);
        process.setProcessEnvironment(environment);
        process.setStandardInputFile(QProcess::nullDevice()); process.setStandardOutputFile(QProcess::nullDevice()); process.setStandardErrorFile(QProcess::nullDevice());
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* value) { value->inheritHandles = false; });
        qint64 pid = 0; check(process.startDetached(&pid), "PotPlayer launch failed");
        auto instance = std::make_unique<Instance>(); instance->pid = DWORD(pid); instance->role = role;
        instance->handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, instance->pid);
        check(instance->handle != nullptr, "Cannot retain launched process identity");
        FILETIME created{}, exit{}, kernel{}, user{}; check(GetProcessTimes(instance->handle, &created, &exit, &kernel, &user), "Cannot read creation timestamp");
        instance->created = (quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime;
        wchar_t path[32768]; DWORD size = 32768;
        check(QueryFullProcessImageNameW(instance->handle, 0, path, &size), "Cannot validate process executable");
        check(QFileInfo(QString::fromWCharArray(path, int(size))).canonicalFilePath().compare(QFileInfo(executable_).canonicalFilePath(), Qt::CaseInsensitive) == 0, "Executable identity mismatch");
        return instance;
    }
    void openOwned(const QString& stage) {
        owned_ = launch("owned", "owned-B.avi", "/new");
        check(owned_->pid != sentinel_->pid, "New instance reused the sentinel");
        after(2500, [this, stage] {
            check(sentinel_->alive() && owned_->alive() && owned_->window(), "Independent instances did not remain alive");
            stage_ = stage; closing_ = false; layer_ = true; emit changed();
            event("independent-ready", {{"stage", stage}, {"sentinel", sentinel_->info()}, {"owned", owned_->info()}, {"smtc", mediaSessions()}});
            notifier_ = std::make_unique<QWinEventNotifier>(owned_->handle);
            connect(notifier_.get(), &QWinEventNotifier::activated, this, [this] { ownedExited(); });
        });
    }
    void ownedExited() {
        notifier_->setEnabled(false); layer_ = false; emit changed();
        event("owned-exited", {{"stage", stage_}, {"owned", owned_->info()}, {"sentinel", sentinel_->info()},
            {"layerVisible", layer_}, {"normalCloseElapsedMs", closing_ ? closeAt_.elapsed() : -1}, {"smtc", mediaSessions()}});
        if (!sentinel_->alive()) { fail("Closing owned instance also closed sentinel"); return; }
        ++completed_;
        if (completed_ == 1) after(1000, [this] { notifier_.reset(); owned_.reset(); openOwned("player-window-close"); });
        else {
            sentinel_->close();
            after(2000, [this] {
                check(!sentinel_->alive(), "Sentinel cleanup was not acknowledged");
                stage_ = "passed"; event("passed", {{"completedDirections", completed_}}); QCoreApplication::quit();
            });
        }
    }
    void event(const QString& name, QJsonObject fields = {}) {
        fields.insert("event", name); fields.insert("elapsedMs", elapsed_.elapsed()); events_.append(fields);
        QSaveFile file(QDir(directory_).filePath("result.json"));
        if (file.open(QIODevice::WriteOnly)) { file.write(QJsonDocument(QJsonObject{{"stage", stage_}, {"layerVisible", layer_}, {"events", events_}}).toJson()); file.commit(); }
    }
    void fail(const QString& message) {
        if (failed_) return; failed_ = true; stage_ = "failed"; layer_ = false; emit changed();
        event("failed", {{"reason", message}});
        if (owned_) owned_->close(); if (sentinel_) sentinel_->close();
        QTimer::singleShot(1500, qApp, [] { QCoreApplication::exit(1); });
    }
    QString executable_, directory_, stage_ = "starting";
    bool layer_{}, closing_{}, failed_{}; int completed_{};
    bool ipc_{}; qint64 initialPosition_{};
    QElapsedTimer elapsed_ = [] { QElapsedTimer value; value.start(); return value; }(), closeAt_;
    QJsonArray events_;
    std::unique_ptr<Instance> sentinel_, forwarded_, owned_;
    std::unique_ptr<QWinEventNotifier> notifier_;
};

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv); app.setQuitOnLastWindowClosed(false);
    if (argc != 3 && (argc != 4 || QByteArray(argv[3]) != "--ipc")) return 2;
    const auto executable = QFileInfo(QString::fromLocal8Bit(argv[1])).canonicalFilePath();
    const auto directory = QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath();
    if (executable.isEmpty() || !QDir().mkpath(directory)) return 2;
    if (!onlyExpectedProcesses({})) { qCritical("Close pre-existing PotPlayer instances before opting into this probe."); return 2; }
    const bool ipc = argc == 4;
    Probe probe(executable, directory, ipc); QQmlApplicationEngine engine;
    if (!ipc) {
        engine.setInitialProperties({{"probe", QVariant::fromValue(&probe)}}); engine.load(QUrl("qrc:/Probe.qml"));
        if (engine.rootObjects().isEmpty()) return 2;
    }
    QTimer::singleShot(0, &probe, [&] { probe.start(); });
    return app.exec();
}
#include "Probe.moc"
