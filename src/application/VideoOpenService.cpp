#include "application/VideoOpenService.h"
#include "platform/windows/PotPlayerLauncher.h"
#include <QFileInfo>
#include <QUrl>
#include <cmath>

namespace {
QString localFile(const QString& input) {
    const QUrl url(input);
    return url.isLocalFile() ? url.toLocalFile() : input;
}
QString normalizedTitle(const QString& title) {
    return title.trimmed().normalized(QString::NormalizationForm_C).toCaseFolded();
}
}
VideoOpenService::VideoOpenService(QObject* parent, Hooks hooks, int timeoutMs) : QObject(parent), hooks_(std::move(hooks)) {
    if (!hooks_.discover) hooks_.discover = PotPlayerLauncher::discover;
    if (!hooks_.preflight) hooks_.preflight = PotPlayerLauncher::preflight;
    if (!hooks_.launch) hooks_.launch = PotPlayerLauncher::launch;
    deadline_.setSingleShot(true); deadline_.setInterval(timeoutMs);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        fail(QStringLiteral("未能确认目标视频已就绪；请检查 PotPlayer 的 SMTC 和打开文件设置，或关闭其他实例后重试"));
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
    if (path == playerPath_) return;
    if (pending_ || active_) cancel();
    playerPath_ = path; emit changed();
}
void VideoOpenService::choosePlayer(const QString& input) {
    const auto path = localFile(input);
    const auto error = PotPlayerLauncher::validate(path);
    if (!error.isEmpty()) { emit failed(error); return; }
    const auto waiting = waitingFile_;
    configurePath(QFileInfo(path).canonicalFilePath());
    needsPlayer_ = false; status_ = QStringLiteral("已绑定 PotPlayer");
    emit playerPathSelected(playerPath_); emit changed();
    if (!waiting.isEmpty()) open({QUrl::fromLocalFile(waiting)});
}
void VideoOpenService::discoverPlayer() {
    const auto path = hooks_.discover();
    if (path.isEmpty()) { status_ = QStringLiteral("未发现唯一的 PotPlayer，请选择程序路径"); emit changed(); return; }
    choosePlayer(path);
}
void VideoOpenService::open(const QVariantList& files) {
    const auto path = videoPath(files);
    if (path.isEmpty()) { emit failed(QStringLiteral("请一次拖入一个支持的本地视频文件")); return; }
    waitingFile_.clear(); needsPlayer_ = false;
    if (playerPath_.isEmpty()) {
        const auto discovered = hooks_.discover();
        if (discovered.isEmpty()) {
            cancel(); waitingFile_ = path; needsPlayer_ = true;
            status_ = QStringLiteral("请选择 PotPlayer 程序，选择后继续打开视频"); emit changed(); emit playerRequired(); return;
        }
        choosePlayer(discovered);
    }
    auto error = PotPlayerLauncher::validate(playerPath_);
    if (error.isEmpty()) error = hooks_.preflight(playerPath_);
    if (!error.isEmpty()) { emit failed(error); return; }
    // The open command is not a media identity. Even a fresh read of the old
    // title must not reach automatic matching during the handoff.
    baselineTitle_.clear();
    deadline_.stop(); pending_ = true; active_ = false; needsPlayer_ = false; waitingFile_.clear(); video_ = path;
    awaitingBaseline_ = true;
    status_ = QStringLiteral("正在连接 PotPlayer，确认原媒体状态…");
    openedAt_ = MediaMonitor::monotonicSeconds();
    deadline_.start();
    emit opening(QStringLiteral("PotPlayerMini64.exe")); emit changed();
}
void VideoOpenService::observe(const MediaSample& sample) {
    latest_ = sample;
    if (pending_) emit changed();
    if (pending_ && awaitingBaseline_) {
        const double age = MediaMonitor::monotonicSeconds() - sample.observedAt;
        if (sample.id != QStringLiteral("PotPlayerMini64.exe") || !std::isfinite(sample.observedAt) ||
            sample.observedAt <= openedAt_ || age < 0 || age > 2.5) return;
        // Selection starts before launch, so this cannot be a read of content
        // opened by our command. Capture even Opened/Stopped and absent sessions.
        baselineTitle_ = sample.found ? normalizedTitle(sample.title) : QString();
        auto error = hooks_.preflight(playerPath_);
        if (!error.isEmpty()) { fail(error); return; }
        awaitingBaseline_ = false; openedAt_ = MediaMonitor::monotonicSeconds();
        error = hooks_.launch(playerPath_, {video_, QStringLiteral("/current")});
        if (!error.isEmpty()) { fail(error); return; }
        status_ = QStringLiteral("等待 PotPlayer 确认目标视频…"); emit changed(); return;
    }
    if (!matchesTarget(sample)) return;
    const auto title = normalizedTitle(sample.title);
    // SMTC has no full path: same-title content cannot prove a completed handoff.
    if (title == baselineTitle_) {
        status_ = QStringLiteral("当前媒体与目标文件同名；请确认 PotPlayer 画面后选择“使用当前视频”");
        emit changed(); return;
    }
    deadline_.stop(); pending_ = false; active_ = true;
    status_ = QStringLiteral("视频已就绪，正在自动匹配弹幕"); emit changed(); emit confirmed();
}
bool VideoOpenService::matchesTarget(const MediaSample& sample) const {
    if (!pending_ || awaitingBaseline_ || sample.id != QStringLiteral("PotPlayerMini64.exe") || !sample.found ||
        sample.observedAt <= openedAt_ || !std::isfinite(sample.observedAt) ||
        !std::isfinite(sample.position) || !std::isfinite(sample.duration) || sample.duration <= 0 ||
        (sample.playback != MediaSample::Playback::Playing && sample.playback != MediaSample::Playback::Paused)) return false;
    const double age = MediaMonitor::monotonicSeconds() - sample.observedAt;
    if (age < 0 || age > 2.5) return false;
    const auto title = normalizedTitle(sample.title);
    const QFileInfo file(video_);
    return title == normalizedTitle(file.fileName()) || title == normalizedTitle(file.completeBaseName());
}
bool VideoOpenService::needsConfirmation() const {
    return !baselineTitle_.isEmpty() && normalizedTitle(latest_.title) == baselineTitle_ && matchesTarget(latest_);
}
void VideoOpenService::confirmCurrent() {
    if (!needsConfirmation()) return;
    baselineTitle_.clear(); observe(latest_);
}
void VideoOpenService::cancel() {
    const bool wasOpen = pending_ || active_ || needsPlayer_;
    deadline_.stop(); pending_ = active_ = needsPlayer_ = awaitingBaseline_ = false; waitingFile_.clear(); video_.clear();
    if (wasOpen) { status_ = QStringLiteral("已取消视频弹幕工作流"); emit changed(); emit cancelled(); }
}
void VideoOpenService::fail(const QString& error) {
    cancel(); status_ = error; emit changed(); emit failed(error);
}
