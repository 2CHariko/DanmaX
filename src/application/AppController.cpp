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
#include <QScopedValueRollback>
#include <QUrl>
#include <algorithm>
#include <cmath>
AppController::AppController(QString dataDirectory, QObject* parent, ForegroundQuery foregroundQuery,
                             QString cacheDirectory, MainForegroundQuery mainForegroundQuery,
                             ToolbarPointerMonitor::CursorQuery cursorQuery, WindowLayerController::Positioner positioner)
    : QObject(parent), settings_(dataDirectory),
#ifdef DANMAKU_STATIC_PORTABLE
      logs_(dataDirectory),
#else
      logs_(QDir(dataDirectory).filePath("logs")),
#endif
      library_(cacheDirectory.isEmpty() ? QDir(dataDirectory).filePath("cache") : cacheDirectory, this),
      metadata_(cacheDirectory.isEmpty() ? QDir(dataDirectory).filePath("cache") : cacheDirectory, this),
      monitor_(this),
      foregroundQuery_(foregroundQuery ? std::move(foregroundQuery) : ForegroundQuery(MediaMonitor::targetForeground)),
      mainForegroundQuery_(mainForegroundQuery ? std::move(mainForegroundQuery) : MainForegroundQuery([](QWindow* window) {
          return window && window->handle() && MediaMonitor::windowOrOwnerForeground(window->winId());
      })), toolbarPointer_(this, std::move(cursorQuery)), windowLayers_(this, std::move(positioner)) {
    connect(&logs_, &LogModel::writeFailed, this, [this](const QString& message) {
        error_ = message;
        emit stateChanged();
    });
    LogModel::install(&logs_);
    connect(&windowLayers_, &WindowLayerController::invalidated, this, [this] { updatePresentationVisibility(true); });
    connect(&windowLayers_, &WindowLayerController::operationFailed, this,
            [this](const QString& message) { logs_.append("WARN", message); });
    file_ = settings_.values()["lastFile"].toString();
    library_.setServers(settings_.values()["danmakuServers"].toStringList());
    connect(&library_, &DanmakuLibrary::sourceLoadStarted, this, [this] {
        discardLoad();
        onlineLoadGeneration_ = loadGeneration_;
        emit stateChanged();
    });
    connect(&library_, &DanmakuLibrary::sourceReady, this, [this] {
        auto result = library_.takeReadyResult();
        if (!result || onlineLoadGeneration_ != loadGeneration_) return;
        const auto title = result->metadata.value("animeTitle").toString() + QStringLiteral(" · ") +
                           result->metadata.value("episodeTitle").toString();
        commitItems(std::move(result->items), title);
        emit loadCompleted(true);
    });
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
            [this](const QString& message) {
                // Poll failures are recoverable. onSample enters WaitingForPlayer;
                // do not leave a fatal error banner after the next healthy sample.
                if (!manual_) logs_.append("WARNING", QStringLiteral("媒体接口（自动重试）：") + message);
            });
    connect(qGuiApp, &QGuiApplication::screenAdded, this, [this] {
        emit screensChanged();
        updateWindow();
    });
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this] {
        emit screensChanged();
        updateWindow();
    });
    connect(qGuiApp, &QGuiApplication::fontDatabaseChanged, this, [this] {
        if (fontsLoaded_) refreshFontFamilies();
    });
    timer_.setTimerType(Qt::PreciseTimer);
    monotonicTime_.start();
    timer_.setInterval(33);
    // Hidden/paused windows have no continuous frame callbacks. Keep media and
    // foreground tracking alive without driving a second visible animation loop.
    connect(&timer_, &QTimer::timeout, this, [this] {
        updatePresentationVisibility();
        if (!overlay_ || !overlay_->isVisible() || !overlay_->isExposed() || !visible_ || !playing_) {
            ++maintenanceTicks_;
            tick();
        } else if (frameTime_.isValid() && frameTime_.elapsed() > 100) {
            // Recovery after exposure/device scheduling changes: request a frame,
            // never advance a second animation clock from this timer.
            if (auto* quick = qobject_cast<QQuickWindow*>(overlay_.data()))
                quick->update();
        }
    });
    metricsTimer_.setInterval(1000);
    connect(&metricsTimer_, &QTimer::timeout, this, &AppController::updateMetrics);
    metricsTime_.start();
    metricsTimer_.start();
    configure();
    logs_.append("INFO", QStringLiteral("C++ / Qt FluentWinUI3 已启动"));
}
AppController::~AppController() {
    windowLayers_.attach(nullptr, nullptr, nullptr);
    settings_.flushPending();
    discardLoad();
    library_.cancel();
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
QStringList AppController::fontFamilies() {
    // Do not emit a change signal while QML is evaluating the initial binding.
    if (!fontsLoaded_) refreshFontFamilies(false);
    return fontFamilies_;
}
void AppController::refreshFontFamilies(bool notify) {
    auto families = QFontDatabase::families();
    families.removeIf([](const QString& family) { return QFontDatabase::isPrivateFamily(family); });
    families.sort(Qt::CaseInsensitive);
    fontsLoaded_ = true;
    if (fontFamilies_ == families) return;
    fontFamilies_ = std::move(families);
    if (notify) emit fontFamiliesChanged();
}
void AppController::attach(QObject* renderer, QObject* overlay, QObject* toolbar, QObject* mainWindow) {
    if (mainWindow_) disconnect(mainWindow_, nullptr, this, nullptr);
    renderer_ = qobject_cast<DanmakuItem*>(renderer);
    overlay_ = qobject_cast<QWindow*>(overlay);
    toolbarWindow_ = qobject_cast<QWindow*>(toolbar);
    mainWindow_ = qobject_cast<QWindow*>(mainWindow);
    if (!renderer_ || !overlay_) {
        fail(QStringLiteral("弹幕窗口初始化失败"));
        return;
    }
    toolbarInputFilter_.attach(toolbarWindow_);
    toolbarPointer_.attach(overlay_, toolbarWindow_);
    windowLayers_.attach(mainWindow_, overlay_, toolbarWindow_);
    if (mainWindow_) {
        connect(mainWindow_, &QWindow::activeChanged, this, [this] { updatePresentationVisibility(true); });
        connect(mainWindow_, &QWindow::visibleChanged, this, [this] { updatePresentationVisibility(true); });
        connect(mainWindow_, &QWindow::windowStateChanged, this, [this] { updatePresentationVisibility(true); });
        connect(mainWindow_, &QObject::destroyed, this, [this] { updatePresentationVisibility(true); });
    }
    connect(overlay_, &QWindow::widthChanged, this, [this] { configure(); });
    connect(overlay_, &QWindow::heightChanged, this, [this] { configure(); });
    connect(overlay_, &QWindow::visibleChanged, this, [this](bool shown) {
        windowLayers_.reconcile(settings_.values()["onTop"].toInt(), panelForeground_, desiredOverlayAllowed_);
        toolbarPointer_.setEnabled(toolbarAllowed_ && shown);
        snapshotDirty_ = true;
        if (shown && running_ && visible_ && renderer_) {
            // Replace the last visible snapshot before exposure, including paused reappearance.
            renderer_->present(engine_);
        }
    });
    if (auto* quick = qobject_cast<QQuickWindow*>(overlay)) {
        connect(quick, &QQuickWindow::afterAnimating, this, [this, quick] {
            ++animationCallbacks_;
            if (running_ && visible_ && quick->isVisible() && quick->isExposed() && (playing_ || snapshotDirty_)) {
                if (playing_) ++frameTicks_;
                tick();
            }
        });
        connect(
            quick, &QQuickWindow::frameSwapped, this, [this, quick] {
                ++renderedFrames_;
                // Item::update during afterAnimating can be consumed by the current
                // sync. Request the NEXT frame after presentation, on the GUI thread.
                if (running_ && playing_ && visible_ && quick->isExposed())
                    quick->update();
            }, Qt::QueuedConnection);
    }
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
    library_.setServers(s["danmakuServers"].toStringList());
    logs_.configure(s["logToFile"].toBool(), s["logLevel"].toString());
    if (lastSettings_.value("targetSession") != s["targetSession"]) {
        mediaGeneration_ = monitor_.select(s["targetSession"].toString(), true);
        if (!manual_) {
            mediaIdentity_.clear();
            syncConnected_ = false;
            lastSampleObservedAt_ = 0;
            mediaClock_ = {};
            sampleTime_.invalidate();
            if (running_) {
                playing_ = false;
                position_ = 0;
                duration_ = 0;
                mediaTitle_.clear();
                resetDanmakuTimeline();
                setPlaybackState(WaitingForPlayer);
            }
        }
    }
    if (running_ && !manual_ && lastSettings_.value("timeOffset") != s["timeOffset"]) {
        if (syncConnected_) {
            mediaClock_.advance(monotonicTime_.nsecsElapsed() * 1e-9);
            position_ = mediaClock_.position();
        }
        resetDanmakuTimeline();
        emit metricsChanged();
    }
    if (lastSettings_.value("chineseConversion") != s["chineseConversion"]) {
        applyChineseConversion();
    }
    const QStringList rendererKeys{"fontFamily", "fontSize", "strokeWidth", "speed", "fixedSeconds",
                                   "maxActive", "maxTracks", "lineSpacing", "overlap", "opacity",
                                   "textureBudgetAuto", "textureBudgetMiB"};
    bool changed = appliedRendererSettings_.isEmpty();
    for (const auto& key : rendererKeys)
        if (appliedRendererSettings_.value(key) != s[key]) changed = true;
    if (renderer_) {
        changed = changed || (overlay_ && (appliedRendererSettings_.value("width").toInt() != overlay_->width() ||
                                          appliedRendererSettings_.value("height").toInt() != overlay_->height()));
        if (changed && !rendererSettingsPending_) {
            // Merge rapid edits and paired width/height notifications before rebuilding geometry.
            rendererSettingsPending_ = true;
            QTimer::singleShot(0, this, [this] {
                if (rendererSettingsPending_) applyRendererSettings();
            });
        }
    }
    lastSettings_ = s;
    updateWindow();
    updatePresentationVisibility(true, false);
    emit stateChanged();
}
bool AppController::updatePresentationVisibility(bool forceForeground, bool notify) {
    if (presentationUpdating_) return false;
    QScopedValueRollback guard(presentationUpdating_, true);
    const bool panelForeground = mainWindow_ && mainWindow_->isVisible() &&
                                mainWindow_->windowState() != Qt::WindowMinimized &&
                                mainForegroundQuery_(mainWindow_);
    // Leaving the panel must not reuse its preceding player foreground sample.
    forceForeground = forceForeground || (panelForeground_ && !panelForeground);
    panelForeground_ = panelForeground;
    const bool checkForeground = !manual_ && settings_.values()["foregroundOnly"].toBool();
    if (running_ && checkForeground &&
        (forceForeground || !foregroundTime_.isValid() || foregroundTime_.elapsed() >= 250)) {
        foreground_ = foregroundQuery_(settings_.values()["targetSession"].toString());
        foregroundTime_.start();
    }
    desiredOverlayAllowed_ = running_ && (!checkForeground || foreground_ || panelForeground_);
    const bool layerReady = windowLayers_.reconcile(settings_.values()["onTop"].toInt(),
                                                   panelForeground_, desiredOverlayAllowed_);
    const bool show = desiredOverlayAllowed_ && (!panelForeground_ || layerReady);
    const bool toolbarAllowed = show;
    const bool changed = show != visible_ || toolbarAllowed != toolbarAllowed_;
    visible_ = show;
    toolbarAllowed_ = toolbarAllowed;
    if (changed) snapshotDirty_ = true;
    toolbarPointer_.setEnabled(toolbarAllowed_ && overlay_ && overlay_->isVisible());
    if (changed && notify) emit stateChanged();
    return changed;
}
void AppController::applyRendererSettings() {
    rendererSettingsPending_ = false;
    if (!renderer_) return;
    const auto s = settings_.values();
    const bool measure = renderer_->configure(s);
    const danmaku::Options o{s["speed"].toDouble(), s["fixedSeconds"].toDouble(), renderer_->trackHeight(),
                            s["maxTracks"].toInt(), s["maxActive"].toInt(), s["overlap"].toBool()};
    const QStringList engineKeys{"speed", "fixedSeconds", "maxTracks", "maxActive", "overlap"};
    bool engineChanged = measure || appliedRendererSettings_.isEmpty();
    for (const auto& key : engineKeys)
        if (s[key] != appliedRendererSettings_.value(key)) engineChanged = true;
    engineChanged = engineChanged || (overlay_ && (appliedRendererSettings_.value("width").toInt() != overlay_->width() ||
                                                  appliedRendererSettings_.value("height").toInt() != overlay_->height()));
    if (engineChanged)
        engine_.reconfigure(o, overlay_ ? overlay_->width() : 1920, overlay_ ? overlay_->height() : 1080,
                            measure ? danmaku::Engine::Measure([this](const auto& item) { return renderer_->measure(item); })
                                    : danmaku::Engine::Measure{});
    // Present even while paused; appearance edits must not depend on playback resuming.
    renderer_->present(engine_);
    appliedRendererSettings_ = s;
    if (overlay_) {
        appliedRendererSettings_["width"] = overlay_->width();
        appliedRendererSettings_["height"] = overlay_->height();
    }
    metrics_["active"] = static_cast<int>(engine_.activeCount());
    metrics_["retiredBySettings"] = static_cast<qulonglong>(engine_.retiredBySettings());
    emit metricsChanged();
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
    if (toolbarWindow_) {
        if (toolbarWindow_->screen() != screen)
            toolbarWindow_->setScreen(screen);
        if (toolbarWindow_->flags().testFlag(Qt::WindowStaysOnTopHint) != top)
            toolbarWindow_->setFlag(Qt::WindowStaysOnTopHint, top);
    }
}
void AppController::loadFile(const QString& input) {
    const auto path = localPath(input);
    if (path.isEmpty())
        return;
    unload();
    const auto generation = ++loadGeneration_;
    loading_ = true;
    progress_ = 0;
    error_.clear();
    status_ = QStringLiteral("读取并索引 XML…");
    emit stateChanged();
    pendingLoad_ = std::make_shared<XmlResult>();
    loader_ = std::jthread([this, path, generation, pending = pendingLoad_](std::stop_token token) {
        *pending = readDanmakuXml(path, token, [this, generation](int progress) {
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
            [this, path, generation, weak = std::weak_ptr<XmlResult>(pending)] {
                if (generation != loadGeneration_)
                    return;
                const auto pending = weak.lock();
                if (!pending) return;
                auto& result = *pending;
                pendingLoad_.reset();
                loading_ = false;
                if (result.cancelled) {
                    status_ = QStringLiteral("已取消载入");
                    emit stateChanged();
                    emit loadCompleted(false);
                    return;
                }
                if (!result.error.isEmpty() || result.items.empty()) {
                    status_ = QStringLiteral("载入失败");
                    fail(result.error.isEmpty() ? QStringLiteral("没有可播放的弹幕") : result.error);
                    emit loadCompleted(false);
                    return;
                }
                commitItems(std::move(result.items), path);
                file_ = path;
                settings_.setValue("lastFile", path);
                status_ = QStringLiteral("已载入 %1 条，过滤 %2 条").arg(total()).arg(result.skipped);
                if (result.skipped > 0)
                    status_ += QStringLiteral("（无效 %1，不支持的模式 %2）")
                                   .arg(result.invalidRecords).arg(result.unsupportedModes);
                if (result.sanitizedCharacters > 0)
                    status_ += QStringLiteral("，修复 %1 个非法控制字符").arg(result.sanitizedCharacters);
                if (result.defaultedColors > 0)
                    status_ += QStringLiteral("，%1 条缺失颜色使用白色").arg(result.defaultedColors);
                logs_.append("INFO", status_);
                emit stateChanged();
                emit loadCompleted(true);
            },
            Qt::QueuedConnection);
    });
}
void AppController::cancelLoad() {
    discardLoad();
    library_.cancel();
    status_ = QStringLiteral("已取消载入");
    emit stateChanged();
}
void AppController::discardLoad() {
    ++loadGeneration_;
    loader_.request_stop();
    if (loader_.joinable()) loader_.join();
    loader_ = std::jthread{};
    // Queued completion callbacks hold only a weak pointer, never the XML payload.
    pendingLoad_.reset();
    loading_ = false;
    progress_ = 0;
}
void AppController::start(bool manual) {
    if (running_) return;
    if (rendererSettingsPending_) applyRendererSettings();
    if (loading_ || engine_.items().empty() || !renderer_) {
        fail(QStringLiteral("请先载入有效弹幕文件"));
        return;
    }
    manual_ = manual;
    position_ = 0;
    duration_ = manual ? loadedDuration_ : 0;
    mediaClock_ = {};
    syncConnected_ = false;
    lastSampleObservedAt_ = 0;
    mediaGeneration_ = monitor_.select(settings_.values()["targetSession"].toString(), true);
    awaitingInitialExposure_ = manual && overlay_ && !overlay_->isExposed();
    if (auto* quick = qobject_cast<QQuickWindow*>(overlay_.data())) {
        quick->setPersistentSceneGraph(true);
        quick->setPersistentGraphics(true);
    }
    running_ = true;
    playing_ = manual;
    snapshotDirty_ = true;
    rate_ = 1;
    mediaIdentity_.clear();
    sampleTime_.invalidate();
    engine_.seek(danmakuPosition());
    renderer_->present(engine_);
    updatePresentationVisibility(true, false);
    setPlaybackState(manual ? Playing : WaitingForPlayer);
    frameTime_.start();
    timer_.start();
    emit stateChanged();
    emit metricsChanged();
}
void AppController::stop() {
    library_.cancel();
    discardLoad();
    resetPlayback(false);
}
void AppController::unload() {
    library_.cancel();
    discardLoad();
    resetPlayback(true);
}
void AppController::setManualMode(bool manual) {
    if (running_ || manual_ == manual) return;
    manual_ = manual;
    emit stateChanged();
}
void AppController::commitItems(std::vector<danmaku::Item> items, const QString& title) {
    resetPlayback(true);
    rawItems_ = items;
    auto convertedItems = std::move(items);
    const auto mode = danmax::parseChineseConversion(settings_.values()["chineseConversion"].toString());
    if (mode != danmax::ChineseConversion::Off) {
        for (auto& item : convertedItems) {
            item.text = danmax::convertChinese(item.text, mode);
        }
    }
    engine_.load(std::move(convertedItems));
    sourceTitle_ = title;
    loadedDuration_ = duration_ = engine_.items().empty() ? 0 : engine_.items().back().time + 15;
    playbackState_ = Ready;
    status_ = QStringLiteral("已载入 %1 条，选择播放方式后开始播放").arg(total());
    error_.clear();
    logs_.append("INFO", status_);
    emit stateChanged();
}
void AppController::applyChineseConversion() {
    if (rawItems_.empty()) return;
    const auto mode = danmax::parseChineseConversion(settings_.values()["chineseConversion"].toString());
    std::vector<std::string> convertedTexts;
    convertedTexts.reserve(rawItems_.size());
    for (const auto& item : rawItems_) {
        convertedTexts.push_back(danmax::convertChinese(item.text, mode));
    }
    engine_.updateItemTexts(convertedTexts);
    if (renderer_) {
        renderer_->invalidateLayoutCache();
        applyRendererSettings();
    }
}
void AppController::resetPlayback(bool unloadSource) {
    timer_.stop();
    running_ = playing_ = visible_ = demo_ = false;
    toolbarAllowed_ = desiredOverlayAllowed_ = false;
    windowLayers_.reconcile(settings_.values()["onTop"].toInt(), false, false);
    toolbarPointer_.setEnabled(false);
    awaitingInitialExposure_ = false;
    snapshotDirty_ = true;
    if (unloadSource) {
        engine_.unload();
        rawItems_.clear();
        sourceTitle_.clear();
        loadedDuration_ = 0;
    } else engine_.seek(0);
    if (renderer_) renderer_->clearContent();
    if (auto* quick = qobject_cast<QQuickWindow*>(overlay_.data())) {
        // Hiding alone retains Qt's scene graph. Release it on its own render thread,
        // also when foreground tracking has already hidden the overlay.
        quick->setPersistentSceneGraph(false);
        quick->setPersistentGraphics(false);
        quick->hide();
        quick->releaseResources();
    } else if (overlay_) overlay_->hide();
    mediaClock_ = {};
    syncConnected_ = false;
    lastSampleObservedAt_ = 0;
    mediaGeneration_ = monitor_.select(settings_.values()["targetSession"].toString(), true);
    mediaIdentity_.clear();
    mediaTitle_.clear();
    position_ = samplePosition_ = 0;
    duration_ = loadedDuration_;
    rate_ = 1;
    frameTime_.invalidate();
    sampleTime_.invalidate();
    foregroundTime_.invalidate();
    foreground_ = true;
    std::vector<double>().swap(frameTimes_);
    frames_ = 0;
    engineMs_ = snapshotMs_ = 0;
    setPlaybackState(engine_.items().empty() ? Empty : Ready);
    updateMetrics();
    emit stateChanged();
}
void AppController::togglePause() {
    if (!running_ || !manual_)
        return;
    if (playbackState_ == Completed) {
        position_ = 0;
        duration_ = loadedDuration_;
        resetDanmakuTimeline();
    }
    playing_ = !playing_;
    if (playing_ && renderer_) renderer_->update();
    frameTime_.restart();
    setPlaybackState(playing_ ? Playing : Paused);
    emit stateChanged();
    emit metricsChanged();
}
void AppController::seek(double position) {
    if (!manual_ || !running_ || !std::isfinite(position))
        return;
    position_ = std::clamp(position, 0.0, std::max(0.0, duration_));
    resetDanmakuTimeline();
    frameTime_.restart();
    if (playbackState_ == Completed) {
        setPlaybackState(Paused);
        emit stateChanged();
    }
    emit metricsChanged();
}
void AppController::selectSession(const QString& id) {
    settings_.setValue("targetSession", id);
}
void AppController::setPlaybackState(PlaybackState state) {
    playbackState_ = state;
    switch (state) {
    case Empty: status_ = QStringLiteral("请选择弹幕文件"); break;
    case Ready: status_ = QStringLiteral("弹幕已就绪，可再次开始"); break;
    case Playing: status_ = manual_ ? QStringLiteral("独立播放") : QStringLiteral("跟随播放器"); break;
    case Paused: status_ = manual_ ? QStringLiteral("已暂停") : QStringLiteral("播放器已暂停"); break;
    case Completed: status_ = QStringLiteral("播放完成"); break;
    case WaitingForPlayer: status_ = QStringLiteral("等待所选播放器"); break;
    case PlayerStopped: status_ = QStringLiteral("播放器已停止，等待恢复"); break;
    case SyncTimeout: status_ = QStringLiteral("媒体同步超时"); break;
    }
}
double AppController::danmakuPosition() const {
    return manual_ ? position_ : std::max(0.0, position_ + settings_.values()["timeOffset"].toDouble());
}
void AppController::resetDanmakuTimeline() {
    engine_.seek(danmakuPosition());
    mediaClock_.takeMotion();
    snapshotDirty_ = true;
    if (renderer_) {
        if (danmakuVisible_) renderer_->present(engine_);
        else renderer_->clearContent();
        renderer_->update();
    }
}
void AppController::exportLogs(const QString& path) {
    if (!logs_.exportTo(localPath(path)))
        fail(QStringLiteral("日志导出失败"));
}
void AppController::onSample(const MediaSample& s) {
    if (!running_ || manual_ || s.generation != mediaGeneration_ ||
        s.id != settings_.values()["targetSession"].toString() ||
        !std::isfinite(s.observedAt) || s.observedAt <= lastSampleObservedAt_)
        return;
    const double age = MediaMonitor::monotonicSeconds() - s.observedAt;
    if (age < 0 || age > 2.5) return;
    lastSampleObservedAt_ = s.observedAt;
    sampleAgeAtReceive_ = age;
    ++mediaSamples_;
    if (!s.found || s.playback == MediaSample::Playback::Closed ||
        s.playback == MediaSample::Playback::Changing || s.playback == MediaSample::Playback::Unknown) {
        mediaClock_.freeze(monotonicTime_.nsecsElapsed() * 1e-9);
        if (syncConnected_) position_ = mediaClock_.position();
        syncConnected_ = false;
        playing_ = false;
        sampleTime_.invalidate();
        if (playbackState_ != WaitingForPlayer) {
            setPlaybackState(WaitingForPlayer);
            emit stateChanged();
            emit metricsChanged();
        }
        return;
    }
    if (!std::isfinite(s.position) || !std::isfinite(s.duration)) return;
    const bool samplePlaying = s.playback == MediaSample::Playback::Playing;
    const double rate = std::isfinite(s.rate) && s.rate > 0 ? s.rate : 1;
    // The adapter already projects SMTC's LastUpdatedTime to acquisition time.
    // Keep that sampled boundary exact: a second projection here would turn a
    // time-zero seek into a positive seek and discard its comments.
    double actual = std::max(0.0, s.position);
    if (s.duration > 0) actual = std::min(actual, s.duration);
    const bool stopped = s.playback == MediaSample::Playback::Stopped;
    const bool changed = !syncConnected_ || mediaIdentity_ != s.identity ||
                         (stopped && playbackState_ != PlayerStopped);
    const bool reset = mediaClock_.synchronize(actual, rate, samplePlaying,
                                monotonicTime_.nsecsElapsed() * 1e-9, changed);
    if (reset) {
        ++sampleResets_;
    }
    mediaIdentity_ = s.identity;
    mediaTitle_ = s.title;
    duration_ = s.duration;
    position_ = samplePosition_ = actual;
    rate_ = rate;
    playing_ = samplePlaying;
    syncConnected_ = true;
    if (reset) resetDanmakuTimeline();
    if (playing_ && renderer_) renderer_->update();
    sampleTime_.start();
    setPlaybackState(stopped ? PlayerStopped : playing_ ? Playing : Paused);
    emit stateChanged();
    emit metricsChanged();
}
void AppController::tick() {
    if (!running_ || !renderer_)
        return;
    const bool visibilityChanged = updatePresentationVisibility();
    if (rendererSettingsPending_) applyRendererSettings();
    // Independent playback begins on first exposure. Before that, the hidden-window
    // maintenance path must not consume time-zero comments. Later hides still advance.
    if (awaitingInitialExposure_ && (!overlay_ || !overlay_->isVisible() || !overlay_->isExposed())) {
        frameTime_.restart();
        return;
    }
    const double elapsed = awaitingInitialExposure_ ? 0 : frameTime_.nsecsElapsed() * 1e-9;
    awaitingInitialExposure_ = false;
    frameTime_.restart();
    if (elapsed > 0 && elapsed < 5) {
        frameTimes_.push_back(elapsed * 1000);
        if (frameTimes_.size() > 3600)
            frameTimes_.erase(frameTimes_.begin(), frameTimes_.begin() + 600);
    }
    if (!manual_ && syncConnected_ && sampleTime_.isValid() &&
        sampleTime_.elapsed() * 1e-3 + sampleAgeAtReceive_ > 2.5) {
        mediaClock_.freeze(monotonicTime_.nsecsElapsed() * 1e-9);
        playing_ = false;
        position_ = mediaClock_.position();
        syncConnected_ = false;
        setPlaybackState(SyncTimeout);
        emit stateChanged();
    }
    if (playing_) {
        if (manual_)
            position_ += elapsed;
        else if (sampleTime_.isValid()) {
            mediaClock_.advance(monotonicTime_.nsecsElapsed() * 1e-9);
            position_ = mediaClock_.position();
        }
    }
    if (manual_ && demo_ && position_ > duration_) {
        position_ = 0;
        engine_.seek(0);
    }
    const bool renderable = visible_ && overlay_ && overlay_->isVisible() && overlay_->isExposed();
    const double motion = manual_ ? elapsed : mediaClock_.takeMotion();
    QElapsedTimer stage;
    stage.start();
    engine_.tick(danmakuPosition(), motion, playing_,
                 [this](const auto& item) { return renderer_->measure(item); }, renderable);
    engineMs_ += stage.nsecsElapsed() * 1e-6;
    stage.restart();
    if (renderable && (playing_ || visibilityChanged || snapshotDirty_)) {
        if (danmakuVisible_) {
            renderer_->present(engine_);
        } else {
            renderer_->clearContent();
        }
        snapshotDirty_ = false;
    } else if (!renderable) snapshotDirty_ = true;
    snapshotMs_ += stage.nsecsElapsed() * 1e-6;
    if (manual_ && !demo_ && playing_) {
        if (engine_.finished()) {
            playing_ = false;
            loadedDuration_ = duration_ = position_;
            setPlaybackState(Completed);
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
    metrics_["retiredBySettings"] = static_cast<qulonglong>(engine_.retiredBySettings());
    metrics_["suppressedWhileHidden"] = static_cast<qulonglong>(engine_.suppressedWhileHidden());
    const double interval = metricsTime_.nsecsElapsed() * 1e-9;
    metricsTime_.restart();
    metrics_["updatesPerSecond"] = interval > 0 ? frames_ / interval : 0;
    metrics_["presentedPerSecond"] = interval > 0 ? (renderedFrames_ - lastMetricsRenderedFrames_) / interval : 0;
    lastMetricsRenderedFrames_ = renderedFrames_;
    metrics_["presentedFrames"] = static_cast<qulonglong>(renderedFrames_);
    metrics_["animationCallbacks"] = static_cast<qulonglong>(animationCallbacks_);
    metrics_["mediaSamples"] = static_cast<qulonglong>(mediaSamples_);
    metrics_["sampleResets"] = static_cast<qulonglong>(sampleResets_);
    metrics_["danmakuPosition"] = danmakuPosition();
    metrics_["mediaGeneration"] = static_cast<qulonglong>(mediaGeneration_);
    metrics_["playbackState"] = static_cast<int>(playbackState_);
    metrics_["frameTicks"] = static_cast<qulonglong>(frameTicks_);
    metrics_["maintenanceTicks"] = static_cast<qulonglong>(maintenanceTicks_);
    metrics_["engineMeanMs"] = frames_ ? engineMs_ / frames_ : 0;
    metrics_["snapshotMeanMs"] = frames_ ? snapshotMs_ / frames_ : 0;
    engineMs_ = snapshotMs_ = 0;
    frames_ = 0;
    if (renderer_) {
        metrics_["cacheEntries"] = renderer_->cacheEntries();
        metrics_["cacheHits"] = static_cast<qulonglong>(renderer_->cacheHits());
        metrics_["cacheMisses"] = static_cast<qulonglong>(renderer_->cacheMisses());
        metrics_["renderBackend"] = renderer_->imageBackend() ? "cached-raster" : "qt-text";
        metrics_["snapshotCount"] = renderer_->snapshotCount();
        metrics_["imageNodes"] = renderer_->imageNodeCount();
        metrics_["textFallbackNodes"] = renderer_->sceneEntries() - renderer_->imageNodeCount();
        metrics_["sceneEntries"] = renderer_->sceneEntries();
        metrics_["textureEstimatedBytes"] = renderer_->textureBytes();
        metrics_["textureBudgetBytes"] = renderer_->textureBudgetBytes();
        metrics_["textureDemandBytes"] = renderer_->textureDemandBytes();
        metrics_["textureFallbackReasons"] = renderer_->textureFallbackReasons();
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
    windowLayers_.reconcile(settings_.values()["onTop"].toInt(), panelForeground_, desiredOverlayAllowed_, true);
    updatePresentationVisibility();
    emit metricsChanged();
}
void AppController::beginDemo(int count) {
    if (!renderer_ || !overlay_)
        return;
    if (rendererSettingsPending_) applyRendererSettings();
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
    rawItems_ = items;
    auto convertedItems = std::move(items);
    const auto mode = danmax::parseChineseConversion(settings_.values()["chineseConversion"].toString());
    if (mode != danmax::ChineseConversion::Off) {
        for (auto& item : convertedItems) {
            item.text = danmax::convertChinese(item.text, mode);
        }
    }
    engine_.load(std::move(convertedItems));
    loadedDuration_ = duration_ = 30;
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
        "p95/p99 are GUI tick intervals; presentedPerSecond counts overlay frameSwapped, not optical FPS; CPU normalized across logical processors";
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
void AppController::setDanmakuVisible(bool visible) {
    if (danmakuVisible_ == visible)
        return;
    danmakuVisible_ = visible;
    snapshotDirty_ = true;
    if (!danmakuVisible_ && renderer_) {
        renderer_->clearContent();
    } else if (danmakuVisible_ && renderer_) {
        renderer_->present(engine_);
    }
    if (overlay_) {
        if (auto* quick = qobject_cast<QQuickWindow*>(overlay_))
            quick->update();
    }
    emit danmakuVisibleChanged();
}
void AppController::clearActiveDanmaku() {
    engine_.clear();
    if (renderer_) {
        renderer_->clearContent();
    }
    metrics_["active"] = 0;
    emit metricsChanged();
    snapshotDirty_ = true;
    if (overlay_) {
        if (auto* quick = qobject_cast<QQuickWindow*>(overlay_))
            quick->update();
    }
}
void AppController::stepTimeOffset(double delta) {
    const double cur = settings_.values().value(QStringLiteral("timeOffset")).toDouble();
    const double next = std::clamp(std::round((cur + delta) * 10.0) / 10.0, -120.0, 120.0);
    settings_.setValue(QStringLiteral("timeOffset"), next);
}
void AppController::stepFontSize(int delta) {
    const int cur = settings_.values().value(QStringLiteral("fontSize")).toInt();
    const int next = std::clamp(cur + delta, 10, 72);
    settings_.setValue(QStringLiteral("fontSize"), next);
}
void AppController::showMainWindow() {
    emit requestShowMainWindow();
    // Explicitly opening an already-active panel can raise it without an
    // activation change. Reapply the toolbar order after the QML raise.
    windowLayers_.invalidate();
}
void AppController::showOnlineDanmakuForAnime(const QString& animeTitle, const QString& episodeTitle, double episodeNumber) {
    if (!animeTitle.trimmed().isEmpty()) {
        if (episodeNumber > 0 || !episodeTitle.trimmed().isEmpty())
            library_.searchAnimeForEpisode(animeTitle.trimmed(), episodeTitle, episodeNumber);
        else library_.searchAnime(animeTitle.trimmed());
    }
    emit requestNavigateToPage(1, 1);
}
void AppController::loadCachedDanmaku(const QString& key) {
    if (key.isEmpty()) return;
    library_.loadCached(key);
    emit requestNavigateToPage(1, 2);
}
