#include "application/AutomaticDanmaku.h"
#include <QScopedValueRollback>
#include <cmath>
#include <utility>

AutomaticDanmaku::AutomaticDanmaku(const QString& dataDirectory, const QString& cacheRoot, QObject* parent, int settleMs)
    : QObject(parent), library_(cacheRoot, this, 30000, false), associations_(dataDirectory) {
    settle_.setSingleShot(true); settle_.setInterval(settleMs);
    connect(&settle_, &QTimer::timeout, this, &AutomaticDanmaku::resolve);
    connect(&library_, &DanmakuLibrary::changed, this, &AutomaticDanmaku::libraryChanged);
    connect(&library_, &DanmakuLibrary::sourceReady, this, &AutomaticDanmaku::accept);
    connect(&library_, &DanmakuLibrary::cacheChanged, this, [this] {
        if (phase_ == Cache && !library_.scanning()) resolve();
    });
    state(Waiting, QStringLiteral("自动匹配未开启"));
}
QString AutomaticDanmaku::recognizedTitle() const {
    return parsed_.valid() ? QStringLiteral("%1 · 第 %2 话").arg(parsed_.anime).arg(parsed_.episode) : QString();
}
void AutomaticDanmaku::state(Phase phase, const QString& status) {
    phase_ = phase; status_ = status; emit changed();
    if (phase == Failed || phase == Choice) QTimer::singleShot(0, this, [this] {
        // Library signals can precede its automatic selection/download in the same turn.
        if ((phase_ == Failed || phase_ == Choice) && !library_.busy()) offerRecovery();
    });
}
void AutomaticDanmaku::offerRecovery() {
    if (!fresh() || identity_.isEmpty()) return;
    if (offeredIdentity_ == identity_) return;
    offeredIdentity_ = identity_;
    phase_ = Choice; emit changed();
    recovery_.begin(latest_, status_);
}
void AutomaticDanmaku::openRecovery() {
    if (!fresh()) return;
    offeredIdentity_.clear(); offerRecovery();
}
void AutomaticDanmaku::cancelRecovery() {
    QScopedValueRollback guard(changing_, true);
    settle_.stop(); library_.cancel(); ready_.reset();
    recovery_.finish(true);
    state(Skipped, QStringLiteral("已跳过当前内容的弹幕匹配；下一集继续自动识别"));
}
bool AutomaticDanmaku::fresh() const {
    const double age = MediaMonitor::monotonicSeconds() - latest_.observedAt;
    return enabled_ && !suspended_ && latest_.found && age >= 0 && age <= 2.5 &&
        (latest_.playback == MediaSample::Playback::Playing || latest_.playback == MediaSample::Playback::Paused) &&
        std::isfinite(latest_.position) && std::isfinite(latest_.duration);
}
void AutomaticDanmaku::configure(bool enabled, const QStringList& servers) {
    if (enabled_ == enabled && servers_ == servers) return;
    const bool wasEnabled = enabled_;
    const bool wasSuspended = suspended_ && wasEnabled && enabled;
    recovery_.finish(true);
    reset(false); enabled_ = enabled; servers_ = servers;
    { QScopedValueRollback guard(changing_, true); library_.setServers(servers); }
    if (enabled) library_.refreshCache();
    suspended_ = wasSuspended;
    state(Waiting, enabled ? QStringLiteral("等待指定播放器提供标题") : QStringLiteral("自动匹配未开启"));
    if (wasEnabled && !enabled) suspended_ = false;
    if (suspended_) state(Waiting, QStringLiteral("自动匹配已暂停；手动操作优先，可点击继续自动匹配"));
}
void AutomaticDanmaku::reset(bool invalidateRecovery) {
    QScopedValueRollback guard(changing_, true);
    if (invalidateRecovery) recovery_.invalidate();
    offeredIdentity_.clear(); parseResult_ = {};
    settle_.stop(); library_.cancel(); ready_.reset(); latest_ = {}; parsed_ = {}; identity_.clear(); suspended_ = false;
    state(Waiting, enabled_ ? QStringLiteral("等待指定播放器提供标题") : QStringLiteral("自动匹配未开启"));
}
void AutomaticDanmaku::suspend(bool resume) {
    if (!enabled_) return;
    QScopedValueRollback guard(changing_, true);
    if (resume) recovery_.finish(true);
    settle_.stop(); library_.cancel(); ready_.reset(); suspended_ = true;
    state(Waiting, QStringLiteral("自动匹配已暂停；手动操作优先，可点击继续自动匹配"));
}
void AutomaticDanmaku::retry() {
    if (!enabled_) return;
    const auto previous = latest_;
    reset(false);
    library_.refreshCache();
    if (!previous.id.isEmpty()) observe(previous);
}
void AutomaticDanmaku::forgetAssociation() {
    const auto error = associations_.forget(parsed_);
    const auto rawError = associations_.forgetRaw(matchingTitle());
    if (!error.isEmpty() || !rawError.isEmpty()) { state(Failed, error.isEmpty() ? rawError : error); return; }
    if (suspended_ || !fresh()) {
        state(Waiting, suspended_ ? QStringLiteral("已忘记关联；自动匹配仍暂停，可点击继续自动匹配")
                                 : QStringLiteral("已忘记关联，等待指定播放器更新"));
        return;
    }
    QScopedValueRollback guard(changing_, true);
    settle_.stop(); library_.cancel(); ready_.reset();
    emit contentInvalidated();
    state(Choice, QStringLiteral("已忘记本番关联，请搜索并选择正确的动画和剧集"));
}
void AutomaticDanmaku::observe(const MediaSample& sample) {
    recovery_.observe(sample);
    const bool sameTitle = sample.title == latest_.title && sample.id == latest_.id && sample.sessionToken == latest_.sessionToken;
    latest_ = sample;
    if (!enabled_ || suspended_) return;
    if (!fresh()) {
        if (phase_ != Waiting) {
            QScopedValueRollback guard(changing_, true);
            settle_.stop(); library_.cancel(); ready_.reset(); identity_.clear();
            state(Waiting, QStringLiteral("等待指定播放器恢复播放或暂停状态"));
        }
        return;
    }
    if (sameTitle && phase_ != Waiting && !identity_.isEmpty()) return;
    const auto report = identifyMediaTitle(matchingTitle());
    const auto parsed = report.selected;
    const auto identity = sample.id + '\n' + QString::number(sample.sessionToken) + '\n' + (parsed.valid() ? parsed.key() : matchingTitle().left(1024).simplified());
    if (identity == identity_ && phase_ != Waiting) return;
    QScopedValueRollback guard(changing_, true);
    offeredIdentity_.clear();
    settle_.stop(); library_.clearSelection(); ready_.reset(); parsed_ = parsed; parseResult_ = report; identity_ = identity;
    library_.setSearchKeyword(report.searchTitle);
    // Invalidate before any asynchronous preparation, including unrecognized titles.
    emit contentInvalidated();
    state(Settling, QStringLiteral("正在等待标题稳定…"));
    settle_.start();
}
void AutomaticDanmaku::resolve() {
    if (!fresh()) { state(Waiting, QStringLiteral("媒体信息已过期，等待播放器更新")); identity_.clear(); return; }
    const auto rawAssociation = associations_.raw(matchingTitle());
    if (library_.scanning()) { state(Cache, QStringLiteral("正在准备本地弹幕索引…")); return; }
    if (!rawAssociation.isEmpty()) {
        for (const auto& entry : library_.cachedEntries()) {
            const auto cache = entry.toMap();
            if (cache.value("valid").toBool() && cache.value("key") == rawAssociation.value("key")) {
                state(LoadingCache, QStringLiteral("正在载入本文件已确认的缓存…")); library_.loadCached(cache.value("key").toString()); return;
            }
        }
    }
    if (!rawAssociation.isEmpty() && servers_.contains(rawAssociation.value("server").toString())) {
        state(Searching, QStringLiteral("正在读取已确认的本文件关联…"));
        library_.loadKnownEpisode(rawAssociation); return;
    }
    if (!parsed_.valid()) { state(Failed, parseResult_.reason); return; }
    if (library_.scanning()) { state(Cache, QStringLiteral("正在准备本地弹幕索引…")); return; }
    const auto exact = associations_.exact(parsed_);
    const auto series = associations_.series(parsed_);
    QMap<QString, QString> matches;
    for (const auto& value : library_.cachedEntries()) {
        const auto entry = value.toMap();
        if (!entry.value("valid").toBool()) continue;
        const auto entryServer = entry.value("server").toString();
        const bool exactMatch = !exact.isEmpty() && entry.value("key") == exact.value("key");
        const bool sameSeries = canonicalAnimeTitle(entry.value("animeTitle").toString()) == parsed_.animeKey ||
            (!series.isEmpty() && entry.value("animeId") == series.value("animeId") && entryServer == series.value("server").toString());
        const auto episode = danmakuEpisodeNumber(entry.value("episodeTitle").toString(), entry.value("episodeNumber").toDouble());
        if (exactMatch || (sameSeries && (parsed_.explicitEpisode || !series.isEmpty()) && std::abs(episode - parsed_.episode) < 0.0001)) {
            // Copies of one episode on compatible services are equivalent; distinct IDs are ambiguous.
            const auto logical = entry.value("animeId").toString() + ':' + entry.value("episodeId").toString();
            if (!matches.contains(logical) || exactMatch) matches[logical] = entry.value("key").toString();
            if (exactMatch) { matches.clear(); matches[logical] = entry.value("key").toString(); break; }
        }
    }
    if (matches.size() == 1) {
        state(LoadingCache, QStringLiteral("正在载入匹配的本地弹幕…"));
        library_.loadCached(matches.first()); return;
    }
    search(matches.size() > 1);
}
void AutomaticDanmaku::search(bool requireChoice) {
    if (!fresh()) return;
    if (servers_.isEmpty()) { state(Failed, QStringLiteral("没有唯一的可用缓存；在线服务已关闭，可手动载入或配置服务后重试")); return; }
    const auto series = associations_.series(parsed_);
    const auto exact = associations_.exact(parsed_);
    const bool usable = !series.isEmpty() && servers_.contains(series.value("server").toString());
    const auto query = usable ? series.value("animeTitle").toString() : parsed_.anime;
    // The separate library retains ambiguous candidates for user confirmation.
    state(Searching, QStringLiteral("正在匹配在线弹幕…"));
    {
        QScopedValueRollback guard(changing_, true);
        if (!exact.isEmpty() && servers_.contains(exact.value("server").toString())) library_.loadKnownEpisode(exact);
        else if (usable && !requireChoice) library_.loadKnownAnime(series, parsed_.episode);
        else if (parsed_.explicitEpisode && !requireChoice) library_.searchAnimeForEpisode(query, {}, parsed_.episode);
        else library_.searchAnime(query);
    }
    libraryChanged();
}
void AutomaticDanmaku::libraryChanged() {
    if (changing_ || !enabled_ || suspended_ || library_.busy() || phase_ == Loaded || phase_ == Waiting || phase_ == Settling || phase_ == Cache || phase_ == Skipped) return;
    if (!library_.error().isEmpty()) {
        if (phase_ == LoadingCache) { search(); return; }
        state(Failed, library_.error());
    } else if (phase_ == Searching || phase_ == Choice) {
        state(Choice, QStringLiteral("未能唯一匹配当前内容，请选择正确的动画和剧集"));
    } else {
        state(Failed, QStringLiteral("自动匹配已取消，可点击重试"));
    }
}
void AutomaticDanmaku::accept() {
    auto result = library_.takeReadyResult();
    if (!result) return;
    if (!fresh()) { identity_.clear(); state(Waiting, QStringLiteral("媒体信息已过期，等待播放器更新")); return; }
    if (phase_ != LoadingCache && phase_ != Searching && phase_ != Choice && phase_ != Failed) return;
    QString error;
    if (parsed_.valid()) error = associations_.remember(parsed_, result->metadata);
    const auto rawError = associations_.rememberRaw(matchingTitle(), result->metadata, parsed_.animeKey);
    if (error.isEmpty()) error = rawError;
    ready_ = std::move(result);
    state(Loaded, QStringLiteral("已自动载入 %1").arg(recognizedTitle()) + (error.isEmpty() ? QString() : QStringLiteral("；") + error));
    emit sourceReady();
    recovery_.finish(true);
}
std::shared_ptr<OnlineDanmakuResult> AutomaticDanmaku::takeReadyResult() {
    return std::exchange(ready_, {});
}
