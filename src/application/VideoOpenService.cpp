#include "application/VideoOpenService.h"
#include <QFileInfo>
#include <QUrl>
#include <QUuid>
#include <cmath>

namespace {
QString localFile(const QString& input) {
    const QUrl url(input);
    return url.isLocalFile() ? url.toLocalFile() : input;
}

}
VideoOpenService::VideoOpenService(QObject* parent, Hooks hooks, int timeoutMs, ExternalPlayerRegistry registry)
    : QObject(parent), registry_(std::move(registry)), hooks_(std::move(hooks)) {
    deadline_.setSingleShot(true); deadline_.setInterval(timeoutMs);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("未能确认目标视频已就绪；请检查 %1 的连接、播放状态及媒体信息后重试").arg(playerName()));
    });
}
QString VideoOpenService::videoPath(const QVariantList& files) {
    if (files.size() != 1) return {};
    const auto path = localFile(files.first().toString());
    const QFileInfo file(path);
    static const QStringList extensions{"mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "ts", "m2ts"};
    return file.isAbsolute() && file.isFile() && extensions.contains(file.suffix().toLower()) ? file.canonicalFilePath() : QString();
}
bool VideoOpenService::accepts(const QVariantList& files) const { return !videoPath(files).isEmpty(); }
QString VideoOpenService::displayName(const QVariantList& files) const { return QFileInfo(videoPath(files)).fileName(); }
void VideoOpenService::configurePath(const QString& path) {
    auto profile = profile_; profile.executable = path; configureProfile(profile);
}
void VideoOpenService::configureProfile(const ExternalPlayerProfile& profile) {
    if (profile == profile_) return;
    cancel(); profile_ = profile; playerPath_ = profile.executable; emit changed();
}
QString VideoOpenService::executableFilter() const {
    return adapter() ? adapter()->executableFilter() : QStringLiteral("播放器 (*.exe)");
}
QString VideoOpenService::preflight() const {
    return hooks_.preflight ? hooks_.preflight(playerPath_) : adapter()->preflight(profile_);
}
void VideoOpenService::choosePlayer(const QString& input) {
    if (!adapter()) { emit failed(QStringLiteral("播放器配置不可用，请检查设置中的配置错误")); return; }
    const auto path = localFile(input);
    const auto error = adapter()->validate(path);
    if (!error.isEmpty()) { emit failed(error); return; }
    const auto waiting = waitingFile_;
    configurePath(QFileInfo(path).canonicalFilePath());
    needsPlayer_ = false; status_ = QStringLiteral("已绑定 %1").arg(playerName());
    const auto selectedPath = playerPath_;
    emit playerPathSelected(selectedPath); emit changed();
    // A persistence failure may restore the previous configured profile through
    // the synchronous controller connection. Never retry discovery recursively.
    if (playerPath_ != selectedPath) { emit failed(QStringLiteral("播放器路径未保存，请检查配置错误")); return; }
    if (!waiting.isEmpty()) open({QUrl::fromLocalFile(waiting)});
}
void VideoOpenService::discoverPlayer() {
    if (!canDiscover()) { status_ = QStringLiteral("此播放器请手动选择程序路径"); emit changed(); return; }
    const auto path = hooks_.discover ? hooks_.discover() : adapter()->discover();
    if (path.isEmpty()) { status_ = QStringLiteral("未发现唯一的 %1，请选择程序路径").arg(playerName()); emit changed(); return; }
    choosePlayer(path);
}
void VideoOpenService::open(const QVariantList& files) {
    if (!adapter()) { emit failed(QStringLiteral("播放器配置不可用，请检查设置中的配置错误")); return; }
    const auto path = videoPath(files);
    if (path.isEmpty()) { emit failed(QStringLiteral("请一次拖入一个支持的本地视频文件")); return; }
    waitingFile_.clear(); needsPlayer_ = false;
    if (playerPath_.isEmpty()) {
        const auto discovered = hooks_.discover ? hooks_.discover() : adapter()->discover();
        if (discovered.isEmpty()) {
            cancel(); waitingFile_ = path; needsPlayer_ = true;
            status_ = QStringLiteral("请选择 %1 程序，选择后继续打开视频").arg(playerName()); emit changed(); emit playerRequired(); return;
        }
        choosePlayer(discovered);
        if (playerPath_.isEmpty()) return;
    }
    auto error = adapter()->validate(playerPath_);
    if (error.isEmpty()) error = preflight();
    if (!error.isEmpty()) { emit failed(error); return; }
    if (!adapter()->managedPlayback()) { emit failed(QStringLiteral("此播放器未提供受控播放后端")); return; }
    emit validated(path);
    cancel(); video_ = path; pending_ = true; transactionSession_.clear();
    openedAt_ = MediaMonitor::monotonicSeconds(); deadline_.start();
    status_ = QStringLiteral("正在启动并连接 %1…").arg(playerName()); emit changed();
    if (hooks_.launch) {
        // Injected transport for deterministic tests; it has its own transaction
        // identity and never falls back to an application ID/SMTC title match.
        const auto launchError = hooks_.launch(playerPath_, adapter()->arguments(profile_, path));
        if (!launchError.isEmpty()) { fail(launchError); return; }
        backendPrepared("danmax-test:" + QUuid::createUuid().toString(QUuid::Id128));
    } else emit managedOpenRequested(profile_.adapter, playerPath_, path);
}
void VideoOpenService::observe(const MediaSample& sample) {
    latest_ = sample;
    if (pending_) emit changed();
    if (!matchesTarget(sample)) return;
    deadline_.stop(); pending_ = false; active_ = true;
    status_ = QStringLiteral("视频已就绪，正在自动匹配弹幕"); emit changed(); emit confirmed();
}
bool VideoOpenService::matchesTarget(const MediaSample& sample) const {
    if (!pending_ || sample.id != transactionSession_ || !sample.found ||
        sample.observedAt <= openedAt_ || !std::isfinite(sample.observedAt) ||
        !std::isfinite(sample.position) || !std::isfinite(sample.duration) || sample.duration <= 0 ||
        (sample.playback != MediaSample::Playback::Playing && sample.playback != MediaSample::Playback::Paused)) return false;
    const double age = MediaMonitor::monotonicSeconds() - sample.observedAt;
    if (age < 0 || age > 2.5) return false;
    const QUrl uri(sample.mediaUri);
    return uri.isLocalFile() && QFileInfo(uri.toLocalFile()).canonicalFilePath().compare(QFileInfo(video_).canonicalFilePath(), Qt::CaseInsensitive) == 0;
}
void VideoOpenService::cancel() {
    const bool wasOpen = pending_ || active_ || needsPlayer_;
    deadline_.stop(); pending_ = active_ = needsPlayer_ = false; waitingFile_.clear(); video_.clear();
    if (wasOpen) { emit managedOpenCancelled(); status_ = QStringLiteral("已取消视频弹幕工作流"); emit changed(); emit cancelled(); }
}
void VideoOpenService::backendPrepared(const QString& sessionId) {
    if (!pending_ || !adapter()->managedPlayback() || sessionId.isEmpty()) return;
    transactionSession_ = sessionId; status_ = QStringLiteral("等待 %1 确认目标视频…").arg(playerName());
    emit opening(sessionId); emit changed();
}
void VideoOpenService::backendFailed(const QString& error) {
    if (pending_ && adapter() && adapter()->managedPlayback()) fail(error);
}
void VideoOpenService::fail(const QString& error) {
    cancel(); status_ = error; emit changed(); emit failed(error);
}
