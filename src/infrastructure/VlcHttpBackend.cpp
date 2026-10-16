#include "infrastructure/VlcHttpBackend.h"
#include "infrastructure/ExternalPlayer.h"
#include "infrastructure/VlcProtocol.h"
#include "platform/windows/MediaMonitor.h"
#include "platform/windows/VlcLauncher.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QTcpServer>
#include <QUuid>
#include <cmath>

static void initializeVlcBridge() { Q_INIT_RESOURCE(vlc_bridge); }
VlcHttpBackend::VlcHttpBackend(QString directory, QObject* parent, Hooks hooks)
    : MediaPlaybackBackend(parent), directory_(std::move(directory)), hooks_(std::move(hooks)) {
    initializeVlcBridge(); network_.setProxy(QNetworkProxy::NoProxy);
    timer_.setInterval(100); connect(&timer_, &QTimer::timeout, this, &VlcHttpBackend::poll);
}
VlcHttpBackend::~VlcHttpBackend() { invalidate(); }
bool VlcHttpBackend::alive() const {
    return !session_.isEmpty() && (hooks_.alive ? hooks_.alive(session_, executable_) : PlayerProcessIdentity::alive(session_, executable_));
}
void VlcHttpBackend::invalidate() {
    ++epoch_; timer_.stop(); commands_.clear();
    if (reply_) { auto* reply = reply_.data(); reply_.clear(); reply->abort(); reply->deleteLater(); }
    network_.clearConnectionCache();
    body_.clear(); latest_ = {}; latestItem_.clear(); lastRawChangedAt_ = 0;
}
void VlcHttpBackend::select(const QString& id, quint64 generation) {
    if (id == selected_ && generation == generation_) return;
    invalidate(); selected_ = id; generation_ = generation;
    if (id != session_) { pendingOpen_.clear(); openingTarget_.clear(); return; }
    timer_.start(); QTimer::singleShot(0, this, &VlcHttpBackend::poll);
}
void VlcHttpBackend::cancelOpen() {
    pendingOpen_.clear(); openingTarget_.clear(); invalidate();
    if (!selected_.isEmpty() && selected_ == session_) timer_.start();
}
ManagedPlaybackOpen VlcHttpBackend::open(const QString& executable, const QString& video) {
    const auto issue = VlcLauncher::validate(executable);
    if (!issue.isEmpty()) return {{}, issue};
    if (!QFileInfo(video).isAbsolute() || !QFileInfo(video).isFile()) return {{}, QStringLiteral("视频文件不存在")};
    if (directory_.isEmpty() || !QFileInfo(directory_).isAbsolute()) return {{}, QStringLiteral("VLC 运行目录未配置")};
    const auto canonical = QFileInfo(executable).canonicalFilePath();
    const bool reuse = executable_.compare(canonical, Qt::CaseInsensitive) == 0 && alive();
    invalidate(); pendingOpen_ = QUrl::fromLocalFile(QFileInfo(video).canonicalFilePath()).toString(QUrl::FullyEncoded);
    openingTarget_ = pendingOpen_; openedFromItem_.clear(); operation_ = QUuid::createUuid().toString(QUuid::Id128);
    if (!reuse) {
        const auto web = QDir(directory_).filePath("http");
        if (!QDir().mkpath(web)) return {{}, QStringLiteral("无法创建 VLC 接口目录")};
        QFile source(":/vlc/status.json");
        if (!source.open(QIODevice::ReadOnly)) return {{}, QStringLiteral("VLC 接口资源缺失")};
        const auto content = source.readAll();
        QSaveFile output(QDir(web).filePath("status.json"));
        if (!output.open(QIODevice::WriteOnly) || output.write(content) != content.size() || !output.commit())
            return {{}, QStringLiteral("无法准备 VLC 接口资源")};
        QTcpServer reserve;
        if (!reserve.listen(QHostAddress::LocalHost, 0)) return {{}, QStringLiteral("无法分配 VLC 本地端口")};
        port_ = reserve.serverPort(); reserve.close();
        password_ = QUuid::createUuid().toString(QUuid::Id128) + QUuid::createUuid().toString(QUuid::Id128);
        QMap<QString, QString> environment;
        for (const auto* name : {"APPDATA", "LOCALAPPDATA", "TEMP", "TMP"}) {
            const auto cache = QDir(directory_).filePath("runtime/" + QString::fromLatin1(name));
            if (!QDir().mkpath(cache)) return {{}, QStringLiteral("无法创建 VLC 运行缓存目录")};
            environment.insert(QString::fromLatin1(name), QDir::toNativeSeparators(cache));
        }
        const QStringList arguments{"--ignore-config", "--no-one-instance", "--no-one-instance-when-started-from-file",
            "--no-media-library", "--no-metadata-network-access", "--intf=qt", "--extraintf=http", "--no-qt-privacy-ask",
            "--no-qt-video-autoresize", "--http-host=127.0.0.1", "--http-port=" + QString::number(port_),
            "--http-password=" + password_, "--http-src=" + QDir::toNativeSeparators(web)};
        ManagedPlaybackOpen started;
        if (hooks_.launch) started = hooks_.launch(canonical, arguments, environment);
        else {
            qint64 pid = 0; started.error = ExternalPlayerProcess::launch(canonical, arguments, &pid, environment);
            if (started.error.isEmpty()) {
                started.sessionId = PlayerProcessIdentity::session(pid, canonical);
                if (started.sessionId.isEmpty()) started.error = QStringLiteral("无法核实新启动的 VLC 实例");
            }
        }
        if (!started.error.isEmpty()) { pendingOpen_.clear(); return started; }
        session_ = started.sessionId; executable_ = canonical;
        emit sessionsChanged({QVariantMap{{"id", session_}, {"title", "VLC"}, {"label", QStringLiteral("VLC（DanmaX 连接）")}}});
    }
    // select() supplies the facade's new generation before any request is sent.
    return {session_, {}};
}
void VlcHttpBackend::missing() {
    MediaSample sample; sample.id = selected_; sample.generation = generation_;
    sample.observedAt = MediaMonitor::monotonicSeconds(); sample.playback = MediaSample::Playback::Closed;
    latest_ = {}; latestItem_.clear(); emit sampleReceived(sample);
}
void VlcHttpBackend::unavailable(const QString& error) {
    missing(); if (lastError_ != error) { lastError_ = error; emit errorOccurred(error); }
}
void VlcHttpBackend::poll() {
    if (selected_.isEmpty() || selected_ != session_ || reply_) return;
    if (!alive()) {
        pendingOpen_.clear(); timer_.stop(); unavailable(QStringLiteral("VLC 实例已退出，请重新打开视频连接"));
        emit sessionsChanged({}); return;
    }
    const auto owner = hooks_.listener ? hooks_.listener(port_, session_) : PlayerProcessIdentity::listener(port_, session_);
    if (owner != PlayerProcessIdentity::Listener::Owned) {
        if (owner == PlayerProcessIdentity::Listener::Foreign) timer_.stop();
        unavailable(owner == PlayerProcessIdentity::Listener::Foreign ? QStringLiteral("VLC 控制端口被其他进程占用，请重试打开视频")
            : QStringLiteral("正在等待 VLC 本地接口")); return;
    }
    QUrlQuery query;
    if (!pendingOpen_.isEmpty()) {
        query.addQueryItem("command", "open"); query.addQueryItem("uri", QString::fromLatin1(QUrl::toPercentEncoding(pendingOpen_)));
        query.addQueryItem("operation", operation_); request(query, "open"); return;
    }
    if (!commands_.empty()) {
        auto command = std::move(commands_.front()); commands_.pop_front();
        const double age = MediaMonitor::monotonicSeconds() - command.expected.observedAt;
        if (!latest_.found || command.expected.id != selected_ || command.expected.generation != generation_
            || age < 0 || age > 2.5 || command.expected.sessionToken != latest_.sessionToken
            || command.expected.mediaUri != latest_.mediaUri || latestItem_.isEmpty()) {
            emit playbackControlFinished(command.request, false, QStringLiteral("媒体或连接已改变")); return;
        }
        query.addQueryItem("command", command.play ? "resume" : "pause");
        query.addQueryItem("expected_uri", QString::fromLatin1(QUrl::toPercentEncoding(command.expected.mediaUri))); query.addQueryItem("expected_item", latestItem_);
        request(query, "control", command.request); return;
    }
    request(query, "status");
}
void VlcHttpBackend::requestPlayback(MediaSample expected, bool play, quint64 request) {
    if (commands_.size() >= 8 || expected.id != selected_ || expected.generation != generation_ || !alive()) {
        emit playbackControlFinished(request, false, QStringLiteral("播放器连接已改变或控制队列已满")); return;
    }
    commands_.push_back({std::move(expected), play, request}); poll();
}
void VlcHttpBackend::request(QUrlQuery query, const QString& kind, quint64 control) {
    const auto requestId = QString::number(++sequence_); query.addQueryItem("request", requestId);
    QUrl url; url.setScheme("http"); url.setHost("127.0.0.1"); url.setPort(port_); url.setPath("/status.json"); url.setQuery(query);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    request.setRawHeader("Authorization", "Basic " + (":" + password_).toUtf8().toBase64());
    request.setTransferTimeout(1500);
    auto* reply = network_.get(request); reply_ = reply; body_.clear(); const auto epoch = epoch_;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply, epoch] {
        if (epoch != epoch_) return;
        if (body_.size() + reply->bytesAvailable() > 65536) { reply->abort(); return; }
        body_.append(reply->readAll());
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, epoch, requestId, kind, control] {
        reply->deleteLater(); if (epoch != epoch_ || reply_ != reply) return;
        reply_.clear(); const auto payload = body_; body_.clear();
        if (!alive()) { unavailable(QStringLiteral("VLC 实例已退出")); return; }
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto decoded = reply->error() == QNetworkReply::NoError && status == 200
            ? VlcProtocol::decode(payload, requestId, selected_, generation_, MediaMonitor::monotonicSeconds()) : std::nullopt;
        if (!decoded) {
            unavailable(QStringLiteral("VLC 接口响应无效或超时，正在重试"));
            if (control) emit playbackControlFinished(control, false, QStringLiteral("VLC 控制响应无效"));
            return;
        }
        if (kind == "open") {
            if (!decoded->accepted || decoded->previousItem.isEmpty()) {
                unavailable(QStringLiteral("VLC 拒绝打开目标视频")); return;
            }
            openedFromItem_ = decoded->previousItem; pendingOpen_.clear();
        }
        auto sample = decoded->sample;
        if (!openingTarget_.isEmpty()) {
            if (!sample.found || QUrl(sample.mediaUri) != QUrl(openingTarget_) || decoded->item == openedFromItem_) { missing(); return; }
            openingTarget_.clear();
        }
        const bool changed = sample.sessionToken != latest_.sessionToken || sample.position != latest_.position
            || sample.playback != latest_.playback || sample.rate != latest_.rate;
        const double now = sample.observedAt;
        if (changed) lastRawChangedAt_ = now;
        latest_ = sample; latestItem_ = decoded->item;
        // VLC's input clock updates less often than the HTTP timer. Repeating the
        // same raw position would rewind the application's interpolated clock.
        if (sample.playing && !changed && now - lastRawChangedAt_ > 1.0) {
            auto waiting = sample; waiting.found = false; waiting.playback = MediaSample::Playback::Changing;
            emit sampleReceived(waiting);
        } else {
            lastError_.clear();
            if (!sample.playing || changed) emit sampleReceived(sample);
        }
        if (control) emit playbackControlFinished(control, decoded->accepted,
            decoded->accepted ? QString() : QStringLiteral("VLC 内容已改变，未执行控制"));
    });
}
