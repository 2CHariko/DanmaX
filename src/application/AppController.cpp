#include "application/AppController.h"
#include "infrastructure/XmlLoader.h"
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickWindow>
#include <QSaveFile>
#include <QScreen>
#include <QUrl>
#include <algorithm>
#include <cmath>
AppController::AppController(QString dataDirectory, QObject* parent)
    : QObject(parent), settings_(dataDirectory), logs_(QDir(dataDirectory).filePath("logs")), monitor_() {
    connect(&logs_, &LogModel::writeFailed, this, [this](const QString& message) {
        error_ = message;
        emit stateChanged();
    });
    LogModel::install(&logs_);
    file_ = settings_.values()["lastFile"].toString();
    connect(&settings_, &SettingsStore::changed, this, &AppController::configure);
    connect(&settings_, &SettingsStore::errorChanged, this, [this] {
        if (!settings_.error().isEmpty())
            logs_.append("ERROR", settings_.error());
    });
    connect(&monitor_, &MediaMonitor::sessionsChanged, this, [this](QVariantList list) {
        sessions_ = std::move(list);
        emit sessionsChanged();
    });
    connect(&monitor_, &MediaMonitor::sampleReceived, this, &AppController::onSample);
    connect(&monitor_, &MediaMonitor::errorOccurred, this,
            [this](const QString& message) { fail(QStringLiteral("媒体接口：") + message); });
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] {
        emit screensChanged();
        updateWindow();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] {
        emit screensChanged();
        updateWindow();
    });
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(16);
    connect(&timer_, &QTimer::timeout, this, &AppController::tick);
    metricsTimer_.setInterval(1000);
    connect(&metricsTimer_, &QTimer::timeout, this, &AppController::updateMetrics);
    metricsTime_.start();
    metricsTimer_.start();
    configure();
    logs_.append("INFO", QStringLiteral("C++ / Qt FluentWinUI3 已启动"));
}
AppController::~AppController() {
    settings_.flushPending();
    loader_.request_stop();
    if (loader_.joinable())
        loader_.join();
    LogModel::uninstall();
}
QString AppController::localPath(const QString& path) {
    const QUrl url(path);
    return url.isLocalFile() ? url.toLocalFile() : path;
}
QStringList AppController::screens() const {
    QStringList names;
    for (auto* screen : qGuiApp->screens())
        names << QStringLiteral("%1 · %2 × %3")
                     .arg(screen->name())
                     .arg(screen->size().width())
                     .arg(screen->size().height());
    return names;
}
bool AppController::hasFluentIcons() const {
    return QFontDatabase::families().contains("Segoe Fluent Icons");
}
void AppController::attach(QObject* renderer, QObject* overlay) {
    renderer_ = qobject_cast<DanmakuItem*>(renderer);
    overlay_ = qobject_cast<QWindow*>(overlay);
    if (!renderer_ || !overlay_) {
        fail(QStringLiteral("弹幕窗口初始化失败"));
        return;
    }
    connect(overlay_, &QWindow::widthChanged, this, [this] { configure(); });
    connect(overlay_, &QWindow::heightChanged, this, [this] { configure(); });
    if (auto* quick = qobject_cast<QQuickWindow*>(overlay))
        connect(
            quick, &QQuickWindow::frameSwapped, this, [this] { ++renderedFrames_; }, Qt::QueuedConnection);
    configure();
}
void AppController::fail(const QString& message) {
    error_ = message;
    logs_.append("ERROR", message);
    emit stateChanged();
}
void AppController::clearError() {
    error_.clear();
    emit stateChanged();
}
void AppController::configure() {
    const auto s = settings_.values();
    logs_.configure(s["logToFile"].toBool(), s["logLevel"].toString());
    monitor_.select(s["targetSession"].toString());
    if (lastSettings_.value("targetSession") != s["targetSession"]) {
        mediaIdentity_.clear();
        playing_ = false;
        sampleTime_.invalidate();
        if (running_ && !manual_)
            status_ = QStringLiteral("等待所选播放器");
    }
    const QStringList engineKeys{"fontFamily", "fontSize",  "strokeWidth", "speed",  "fixedSeconds",
                                 "maxActive",  "maxTracks", "lineSpacing", "overlap"};
    bool rebuild = lastSettings_.isEmpty();
    for (const auto& key : engineKeys)
        if (lastSettings_.value(key) != s[key])
            rebuild = true;
    if (renderer_) {
        renderer_->configure(s);
        if (rebuild || (overlay_ && (lastSettings_.value("width").toInt() != overlay_->width() ||
                                     lastSettings_.value("height").toInt() != overlay_->height()))) {
            danmaku::Options o{s["speed"].toDouble(),  s["fixedSeconds"].toDouble(), renderer_->trackHeight(),
                               s["maxTracks"].toInt(), s["maxActive"].toInt(),       s["overlap"].toBool()};
            engine_.configure(o, overlay_ ? overlay_->width() : 1920, overlay_ ? overlay_->height() : 1080);
            renderer_->present(engine_);
        }
    }
    lastSettings_ = s;
    if (overlay_) {
        lastSettings_["width"] = overlay_->width();
        lastSettings_["height"] = overlay_->height();
    }
    updateWindow();
    emit stateChanged();
}
void AppController::updateWindow() {
    if (!overlay_)
        return;
    const auto s = settings_.values();
    const auto list = qGuiApp->screens();
    if (list.isEmpty())
        return;
    auto* screen = list[std::clamp(s["screenIndex"].toInt(), 0, static_cast<int>(list.size()) - 1)];
    if (overlay_->screen() != screen)
        overlay_->setScreen(screen);
    if (overlay_->geometry() != screen->geometry())
        overlay_->setGeometry(screen->geometry());
    const bool top = s["onTop"].toInt() > 0;
    if (overlay_->flags().testFlag(Qt::WindowStaysOnTopHint) != top)
        overlay_->setFlag(Qt::WindowStaysOnTopHint, top);
}
void AppController::loadFile(const QString& input) {
    const auto path = localPath(input);
    if (path.isEmpty())
        return;
    loader_.request_stop();
    if (loader_.joinable())
        loader_.join();
    stop();
    const auto generation = ++loadGeneration_;
    loading_ = true;
    progress_ = 0;
    error_.clear();
    status_ = QStringLiteral("读取并索引 XML…");
    emit stateChanged();
    loader_ = std::jthread([this, path, generation](std::stop_token token) {
        auto result = readDanmakuXml(path, token, [this, generation](int progress) {
            QMetaObject::invokeMethod(
                this,
                [this, generation, progress] {
                    if (generation == loadGeneration_) {
                        progress_ = progress;
                        emit stateChanged();
                    }
                },
                Qt::QueuedConnection);
        });
        QMetaObject::invokeMethod(
            this,
            [this, path, generation, result = std::move(result)]() mutable {
                if (generation != loadGeneration_)
                    return;
                loading_ = false;
                if (result.cancelled) {
                    status_ = QStringLiteral("已取消加载");
                    emit stateChanged();
                    emit loadCompleted(false);
                    return;
                }
                if (!result.error.isEmpty() || result.items.empty()) {
                    status_ = QStringLiteral("加载失败");
                    fail(result.error.isEmpty() ? QStringLiteral("没有可播放的弹幕") : result.error);
                    emit loadCompleted(false);
                    return;
                }
                engine_.load(std::move(result.items));
                file_ = path;
                position_ = 0;
                duration_ = engine_.items().back().time + 15;
                demo_ = false;
                settings_.setValue("lastFile", path);
                status_ = QStringLiteral("已加载 %1 条，过滤 %2 条").arg(total()).arg(result.skipped);
                logs_.append("INFO", status_);
                emit stateChanged();
                emit loadCompleted(true);
            },
            Qt::QueuedConnection);
    });
}
void AppController::cancelLoad() {
    ++loadGeneration_;
    loader_.request_stop();
    loading_ = false;
    status_ = QStringLiteral("已取消加载");
    emit stateChanged();
}
void AppController::start(bool manual) {
    if (loading_ || engine_.items().empty() || !renderer_) {
        fail(QStringLiteral("请先加载有效弹幕文件"));
        return;
    }
    if (manual && position_ >= duration_)
        position_ = 0;
    manual_ = manual;
    running_ = true;
    playing_ = manual;
    visible_ = true;
    wasVisible_ = true;
    rate_ = 1;
    mediaIdentity_.clear();
    sampleTime_.invalidate();
    engine_.seek(position_);
    renderer_->present(engine_);
    status_ = manual ? QStringLiteral("独立播放") : QStringLiteral("等待所选播放器");
    frameTime_.start();
    timer_.start();
    emit stateChanged();
}
void AppController::stop() {
    timer_.stop();
    running_ = playing_ = visible_ = false;
    wasVisible_ = false;
    engine_.clear();
    if (renderer_)
        renderer_->present(engine_);
    status_ = total() ? QStringLiteral("已停止") : QStringLiteral("请选择弹幕文件");
    emit stateChanged();
}
void AppController::togglePause() {
    if (!running_ || !manual_)
        return;
    playing_ = !playing_;
    frameTime_.restart();
    status_ = playing_ ? QStringLiteral("独立播放") : QStringLiteral("已暂停");
    emit stateChanged();
}
void AppController::seek(double position) {
    if (!manual_ && running_)
        return;
    position_ = std::clamp(position, 0.0, std::max(0.0, duration_));
    engine_.seek(position_);
    if (renderer_)
        renderer_->present(engine_);
    emit metricsChanged();
}
void AppController::selectSession(const QString& id) {
    settings_.setValue("targetSession", id);
}
void AppController::importIni(const QString& path) {
    if (settings_.importIni(localPath(path)))
        logs_.append("INFO", QStringLiteral("已导入旧 INI；原文件保留"));
}
void AppController::exportLogs(const QString& path) {
    if (!logs_.exportTo(localPath(path)))
        fail(QStringLiteral("日志导出失败"));
}
void AppController::onSample(const MediaSample& s) {
    if (!running_ || manual_)
        return;
    if (!s.found || s.id != settings_.values()["targetSession"].toString()) {
        playing_ = false;
        status_ = QStringLiteral("等待所选播放器");
        sampleTime_.invalidate();
        emit stateChanged();
        return;
    }
    const double actual = std::max(0.0, s.position + settings_.values()["timeOffset"].toDouble());
    const bool changed = mediaIdentity_ != s.identity;
    if (changed || std::abs(actual - position_) > 0.75 || (actual < position_ - 0.25)) {
        engine_.seek(actual);
        if(renderer_)renderer_->present(engine_);
    }
    mediaIdentity_ = s.identity;
    mediaTitle_ = s.title;
    duration_ = s.duration;
    position_ = samplePosition_ = actual;
    rate_ = s.rate;
    playing_ = s.playing;
    sampleTime_.start();
    status_ = playing_ ? QStringLiteral("跟随播放器") : QStringLiteral("播放器已暂停");
    emit stateChanged();
}
void AppController::tick() {
    if (!running_ || !renderer_)
        return;
    const double elapsed = frameTime_.nsecsElapsed() * 1e-9;
    frameTime_.restart();
    if (elapsed > 0 && elapsed < 5) {
        frameTimes_.push_back(elapsed * 1000);
        if (frameTimes_.size() > 3600)
            frameTimes_.erase(frameTimes_.begin(), frameTimes_.begin() + 600);
    }
    if (!manual_ && sampleTime_.isValid() && sampleTime_.elapsed() > 2500) {
        playing_ = false;
        status_ = QStringLiteral("媒体同步超时");
        emit stateChanged();
    }
    if (playing_) {
        if (manual_)
            position_ += elapsed;
        else if (sampleTime_.isValid())
            position_ = samplePosition_ + sampleTime_.nsecsElapsed() * 1e-9 * rate_;
    }
    if (manual_ && demo_ && position_ > duration_) {
        position_ = 0;
        engine_.seek(0);
    }
    const bool checkForeground = !manual_ && settings_.values()["foregroundOnly"].toBool();
    if (checkForeground && (!foregroundTime_.isValid() || foregroundTime_.elapsed() >= 250)) {
        foreground_ = MediaMonitor::targetForeground(settings_.values()["targetSession"].toString());
        foregroundTime_.start();
    }
    const bool show = !checkForeground || foreground_;
    if (show != visible_) {
        visible_ = show;
        emit stateChanged();
    }
    if (!show) {
        wasVisible_ = false;
        return;
    }
    if (!wasVisible_) {
        engine_.seek(position_);
        renderer_->present(engine_);
        wasVisible_ = true;
    }
    engine_.tick(position_, elapsed * (manual_ ? 1 : rate_), playing_,
                 [this](const auto& item) { return renderer_->measure(item); });
    if (playing_)
        renderer_->present(engine_);
    if (manual_ && !demo_ && playing_) {
        if (engine_.finished()) {
            playing_ = false;
            duration_ = position_;
            status_ = QStringLiteral("播放完成");
            emit stateChanged();
        } else if (position_ >= duration_) {
            duration_ = position_ + 1;
            emit stateChanged();
        }
    }
    ++frames_;
}
void AppController::updateMetrics() {
    metrics_ = MediaMonitor::processMetrics();
    metrics_["active"] = static_cast<int>(engine_.activeCount());
    metrics_["dropped"] = static_cast<qulonglong>(engine_.dropped());
    const double interval = metricsTime_.nsecsElapsed() * 1e-9;
    metricsTime_.restart();
    metrics_["updatesPerSecond"] = interval > 0 ? frames_ / interval : 0;
    metrics_["presentedFrames"] = static_cast<qulonglong>(renderedFrames_);
    frames_ = 0;
    if (renderer_) {
        metrics_["cacheEntries"] = renderer_->cacheEntries();
        metrics_["cacheHits"] = static_cast<qulonglong>(renderer_->cacheHits());
        metrics_["cacheMisses"] = static_cast<qulonglong>(renderer_->cacheMisses());
    }
    auto sorted = frameTimes_;
    std::sort(sorted.begin(), sorted.end());
    if (!sorted.empty()) {
        metrics_["p95Ms"] = sorted[static_cast<std::size_t>((sorted.size() - 1) * 0.95)];
        metrics_["p99Ms"] = sorted[static_cast<std::size_t>((sorted.size() - 1) * 0.99)];
    }
    if (overlay_) {
        metrics_["width"] = overlay_->width();
        metrics_["height"] = overlay_->height();
        metrics_["dpr"] = overlay_->devicePixelRatio();
    }
    if (visible_ && overlay_)
        MediaMonitor::maintainTopmost(overlay_->winId(), settings_.values()["onTop"].toInt());
    emit metricsChanged();
}
void AppController::beginDemo(int count) {
    const auto s = settings_.values();
    engine_.configure({s["speed"].toDouble(), s["fixedSeconds"].toDouble(), renderer_->trackHeight(),
                       s["maxTracks"].toInt(), std::clamp(count, 50, 5000), true},
                      overlay_->width(), overlay_->height());
    std::vector<danmaku::Item> items;
    for (int i = 0; i < count; ++i)
        items.push_back({0,
                         static_cast<danmaku::Mode>(i % 12 == 0   ? 5
                                                    : i % 12 == 1 ? 4
                                                                  : 1),
                         QStringLiteral("弹幕测试 %1 · Fluent / Qt Quick").arg(i).toUtf8().toStdString(),
                         0xffffff});
    engine_.load(std::move(items));
    duration_ = 30;
    position_ = 0;
    demo_ = true;
    start(true);
}
void AppController::writeReport(const QString& path) {
    updateMetrics();
    metrics_["sessions"] = sessions_;
    metrics_["mediaTitle"] = mediaTitle_;
    metrics_["position"] = position_;
    metrics_["duration"] = duration_;
    metrics_["playing"] = playing_;
    metrics_["status"] = status_;
    metrics_["qtVersion"] = qVersion();
    metrics_["requestedCount"] = total();
    metrics_["metricNote"] =
        "p95/p99 are GUI tick intervals, not GPU frame times; CPU normalized across logical processors";
    if (overlay_) {
        metrics_["refreshRate"] = overlay_->screen()->refreshRate();
        metrics_.insert(MediaMonitor::windowMetrics(overlay_->winId()));
    }
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject::fromVariantMap(metrics_)).toJson());
        file.commit();
    }
}
