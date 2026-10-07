#include "application/DanmakuLibrary.h"
#include <QDateTime>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRunnable>
#include <QUrlQuery>
#include <cmath>
#include <utility>

namespace {
constexpr qint64 responseLimit = 64LL * 1024 * 1024;
QString identifier(const QJsonValue& value) {
    if (value.isString()) {
        bool ok{}; const auto id = value.toString().toLongLong(&ok);
        return ok && id > 0 ? QString::number(id) : QString();
    }
    const auto id = value.toInteger(-1);
    return id > 0 ? QString::number(id) : QString();
}
} // namespace

DanmakuLibrary::DanmakuLibrary(QString cacheRoot, QObject* parent, int timeoutMs)
    : QObject(parent), directory_(QDir(cacheRoot).filePath("danmaku")), network_(this), timeoutMs_(timeoutMs) {
    worker_.setMaxThreadCount(1);
    scanner_.setMaxThreadCount(1);
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        if (!reply_) return;
        auto* reply = reply_.data();
        reply_ = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort(); reply->deleteLater();
        nextAttempt(QStringLiteral("请求超时，请重试"));
    });
    refreshCache();
}
DanmakuLibrary::~DanmakuLibrary() {
    invalidate();
    ++scanGeneration_;
    scannerStop_.request_stop(); scanner_.clear();
    worker_.waitForDone(); scanner_.waitForDone();
    pendingScan_.reset();
}
void DanmakuLibrary::invalidate() {
    ++generation_;
    attempt_ = {}; attemptErrors_.clear(); attemptIndex_ = -1;
    deadline_.stop();
    if (reply_) {
        auto* reply = reply_.data(); reply_ = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort(); reply->deleteLater();
    }
    workerStop_.request_stop(); worker_.clear();
    pending_.reset(); ready_.reset();
    busy_ = false; progress_ = -1;
}
void DanmakuLibrary::cancel() {
    const bool wasBusy = busy_;
    invalidate();
    if (wasBusy) status_ = QStringLiteral("已取消");
    emit changed();
}
void DanmakuLibrary::begin(const QString& status, bool sourceLoad) {
    invalidate();
    busy_ = true; status_ = status; error_.clear();
    if (sourceLoad) emit sourceLoadStarted();
    emit changed();
}
void DanmakuLibrary::fail(const QString& error) {
    busy_ = false; progress_ = -1; error_ = error; status_ = QStringLiteral("操作失败");
    emit changed();
}
void DanmakuLibrary::setServers(const QStringList& addresses) {
    QStringList servers;
    for (const auto& address : addresses) {
        auto normalized = normalizeDanmakuServer(address);
        while (normalized.endsWith('/')) normalized.chop(1);
        if (normalized.isEmpty() || servers.contains(normalized)) return;
        servers.append(normalized);
    }
    if (servers_ == servers) return;
    invalidate(); servers_ = servers; server_.clear();
    animeId_.clear(); episodeId_.clear(); animes_.clear(); episodes_.clear(); error_.clear();
    status_ = servers_.isEmpty() ? QStringLiteral("请先在设置中填写兼容服务地址") : QStringLiteral("请输入动画名称");
    emit changed(); emit cacheChanged();
}
void DanmakuLibrary::startAttempts(std::function<void()> attempt) {
    attempt_ = std::move(attempt); attemptIndex_ = -1; attemptErrors_.clear();
    operationStatus_ = status_;
    nextAttempt();
}
void DanmakuLibrary::nextAttempt(const QString& reason) {
    if (!attempt_ || !busy_) return;
    if (!reason.isEmpty()) attemptErrors_.append(server_ + QStringLiteral("：") + reason);
    if (++attemptIndex_ >= servers_.size()) {
        attempt_ = {};
        fail(attemptErrors_.isEmpty() ? QStringLiteral("请先在设置中填写兼容服务地址")
                                     : attemptErrors_.join('\n'));
        return;
    }
    server_ = servers_[attemptIndex_]; progress_ = -1;
    status_ = operationStatus_ + QStringLiteral("（%1/%2）\n%3").arg(attemptIndex_ + 1).arg(servers_.size()).arg(server_);
    const auto generation = generation_;
    emit changed();
    if (generation != generation_ || !busy_ || !attempt_) return;
    // Copy before invoking: a synchronous failure may release the stored callback.
    const auto attempt = attempt_;
    attempt();
}
void DanmakuLibrary::request(const QString& path, const QString& query,
                            std::function<void(QByteArray)> completed) {
    if (server_.isEmpty()) { fail(QStringLiteral("请先在设置中填写兼容服务地址")); return; }
    QUrl url(server_ + path);
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "LocalDanmaku/0.2");
    request.setRawHeader("Accept", "application/json");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    request.setTransferTimeout(timeoutMs_);
    auto* reply = network_.get(request);
    reply_ = reply;
    reply->setReadBufferSize(256 * 1024);
    const auto generation = generation_;
    auto bytes = std::make_shared<QByteArray>();
    auto exceeded = std::make_shared<bool>(false);
    const auto consume = [reply, bytes, exceeded] {
        const auto chunk = reply->readAll();
        if (bytes->size() + chunk.size() > responseLimit ||
            reply->header(QNetworkRequest::ContentLengthHeader).toLongLong() > responseLimit) {
            *exceeded = true; reply->abort();
        } else bytes->append(chunk);
    };
    connect(reply, &QIODevice::readyRead, this, consume);
    connect(reply, &QNetworkReply::downloadProgress, this, [this, generation](qint64 received, qint64 total) {
        if (generation != generation_) return;
        progress_ = total > 0 ? static_cast<int>(std::min<qint64>(100, 100 * received / total)) : -1;
        emit changed();
    });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, generation, bytes, exceeded, consume, completed = std::move(completed)]() mutable {
            if (generation != generation_) { reply->deleteLater(); return; }
            deadline_.stop(); consume(); reply_ = nullptr;
            const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            QString error;
            if (*exceeded) error = QStringLiteral("响应超过 64 MiB 限制");
            else if (status == 401 || status == 403)
                error = QStringLiteral("服务需要认证；当前版本未提供账号登录，请选择允许匿名访问的兼容服务");
            else if (status != 200 && status != 0) error = QStringLiteral("服务返回 HTTP %1，请重试").arg(status);
            else if (reply->error() != QNetworkReply::NoError) error = QStringLiteral("网络请求失败：") + reply->errorString();
            else if (status != 200) error = QStringLiteral("服务未返回有效 HTTP 响应");
            reply->deleteLater();
            if (!error.isEmpty()) { nextAttempt(error); return; }
            completed(std::move(*bytes));
        });
    deadline_.start(timeoutMs_);
}
void DanmakuLibrary::work(std::function<Work(std::stop_token)> action, std::function<void(Work)> completed) {
    workerStop_.request_stop(); worker_.clear(); workerStop_ = std::stop_source{};
    const auto generation = generation_;
    pending_ = std::make_shared<Work>();
    worker_.start(QRunnable::create([this, generation, pending = pending_, action = std::move(action),
                            stop = workerStop_.get_token(), completed = std::move(completed)]() mutable {
        *pending = action(stop);
        if (stop.stop_requested()) return;
        QMetaObject::invokeMethod(this, [this, generation, weak = std::weak_ptr<Work>(pending),
                                         completed = std::move(completed)]() mutable {
            if (generation != generation_) return;
            const auto result = weak.lock();
            if (!result) return;
            pending_.reset();
            completed(std::move(*result));
        }, Qt::QueuedConnection);
    }));
}
void DanmakuLibrary::searchAnime(const QString& keyword) {
    begin(QStringLiteral("正在搜索动画…"));
    animeId_.clear(); episodeId_.clear(); animes_.clear(); episodes_.clear(); emit changed(); emit cacheChanged();
    if (keyword.trimmed().isEmpty()) { fail(QStringLiteral("请输入动画名称")); return; }
    if (keyword.size() > 512) { fail(QStringLiteral("搜索关键词过长")); return; }
    QUrlQuery query; query.addQueryItem("keyword", keyword.trimmed());
    startAttempts([this, query] { request("/api/v2/search/anime", query.query(QUrl::FullyEncoded), [this](QByteArray bytes) {
        work([bytes = std::move(bytes)](std::stop_token stop) {
            Work output;
            const auto doc = QJsonDocument::fromJson(bytes);
            const auto object = doc.object();
            if (!doc.isObject() || !object.value("animes").isArray() ||
                (object.contains("success") && !object.value("success").toBool(true))) {
                output.error = QStringLiteral("搜索响应格式错误或服务返回失败"); return output;
            }
            for (const auto& value : object.value("animes").toArray()) {
                if (stop.stop_requested()) return Work{};
                const auto anime = value.toObject();
                const auto id = identifier(anime.value("animeId"));
                const auto title = anime.value("animeTitle").toString().trimmed();
                if (!id.isEmpty() && !title.isEmpty()) output.list.append(QVariantMap{{"id", id}, {"title", title}});
            }
            return output;
        }, [this](Work output) {
            if (!output.error.isEmpty()) { nextAttempt(output.error); return; }
            if (output.list.isEmpty()) { nextAttempt(QStringLiteral("没有可用结果")); return; }
            attempt_ = {};
            animes_ = std::move(output.list); busy_ = false;
            status_ = QStringLiteral("请选择动画"); progress_ = -1;
            emit changed();
        });
    }); });
}
void DanmakuLibrary::selectAnime(const QString& id) {
    bool found{};
    for (const auto& entry : animes_) if (entry.toMap().value("id").toString() == id) found = true;
    if (!found) return;
    begin(QStringLiteral("正在读取剧集…")); animeId_ = id; episodeId_.clear(); episodes_.clear();
    emit changed(); emit cacheChanged();
    startAttempts([this, id] { request("/api/v2/bangumi/" + id, {}, [this](QByteArray bytes) {
        work([bytes = std::move(bytes)](std::stop_token stop) {
            Work output;
            const auto doc = QJsonDocument::fromJson(bytes); const auto object = doc.object();
            auto values = object.value("bangumi").toObject().value("episodes");
            if (!values.isArray()) values = object.value("episodes");
            if (!doc.isObject() || !values.isArray() ||
                (object.contains("success") && !object.value("success").toBool(true))) {
                output.error = QStringLiteral("剧集响应格式错误或服务返回失败"); return output;
            }
            for (const auto& value : values.toArray()) {
                if (stop.stop_requested()) return Work{};
                const auto episode = value.toObject(); const auto id = identifier(episode.value("episodeId"));
                if (id.isEmpty()) continue;
                auto title = episode.value("episodeTitle").toString().trimmed();
                if (title.isEmpty()) title = QStringLiteral("剧集 %1").arg(id);
                output.list.append(QVariantMap{{"id", id}, {"title", title}});
            }
            return output;
        }, [this](Work output) {
            if (!output.error.isEmpty()) { nextAttempt(output.error); return; }
            if (output.list.isEmpty()) { nextAttempt(QStringLiteral("没有可用结果")); return; }
            attempt_ = {};
            episodes_ = std::move(output.list); busy_ = false;
            status_ = QStringLiteral("请选择剧集"); progress_ = -1;
            emit changed();
        });
    }); });
}
void DanmakuLibrary::selectEpisode(const QString& id) {
    for (const auto& entry : episodes_) {
        if (entry.toMap().value("id").toString() != id) continue;
        cancel(); episodeId_ = id; error_.clear(); status_ = QStringLiteral("可下载所选剧集的弹幕");
        emit changed(); emit cacheChanged(); return;
    }
}
QVariantMap DanmakuLibrary::episodeMetadata() const {
    QVariantMap metadata{{"server", server_}, {"animeId", animeId_}, {"episodeId", episodeId_},
                         {"downloadedAt", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};
    for (const auto& entry : animes_) if (entry.toMap().value("id").toString() == animeId_)
        metadata["animeTitle"] = entry.toMap().value("title");
    for (const auto& entry : episodes_) if (entry.toMap().value("id").toString() == episodeId_)
        metadata["episodeTitle"] = entry.toMap().value("title");
    return metadata;
}
bool DanmakuLibrary::selectedCached() const {
    if (episodeId_.isEmpty()) return false;
    for (const auto& server : servers_) {
        const auto key = danmakuCacheKey(server, episodeId_.toLongLong());
        for (const auto& entry : entries_)
            if (entry.toMap().value("key") == key && entry.toMap().value("valid").toBool()) return true;
    }
    return false;
}
void DanmakuLibrary::downloadEpisode(bool refresh) {
    if (animeId_.isEmpty() || episodeId_.isEmpty() || servers_.isEmpty()) { fail(QStringLiteral("请先选择动画和剧集")); return; }
    begin(refresh ? QStringLiteral("正在重新下载…") : QStringLiteral("正在检查缓存…"), true);
    const auto fetch = [this] { startAttempts([this] { fetchEpisode(episodeMetadata()); }); };
    if (refresh) { fetch(); return; }
    work([directory = directory_, servers = servers_, episode = episodeId_](std::stop_token stop) {
        Work output;
        for (const auto& server : servers) {
            if (stop.stop_requested()) return Work{};
            auto result = std::make_shared<OnlineDanmakuResult>(
                readCachedDanmaku(directory, danmakuCacheKey(server, episode.toLongLong()), stop));
            if (result->error.isEmpty() && !result->cancelled) { output.result = std::move(result); break; }
        }
        return output;
    }, [this, fetch](Work output) {
        if (output.result) acceptResult(std::move(output.result));
        else fetch();
    });
}
void DanmakuLibrary::fetchEpisode(QVariantMap metadata) {
    request("/api/v2/comment/" + metadata.value("episodeId").toString(), "withRelated=true&chConvert=0",
        [this, metadata](QByteArray bytes) {
            work([bytes = std::move(bytes), metadata, directory = directory_](std::stop_token stop) {
                Work output; output.result = std::make_shared<OnlineDanmakuResult>(parseOnlineDanmaku(bytes, stop));
                if (!output.result->error.isEmpty() || output.result->cancelled) return output;
                output.result->metadata = metadata;
                output.result->metadata["downloadedAt"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
                output.result->warning = saveCachedDanmaku(directory, *output.result, stop);
                output.result->cancelled = stop.stop_requested();
                return output;
            }, [this](Work output) {
                if (!output.result || output.result->cancelled) return;
                if (!output.result->error.isEmpty()) { nextAttempt(output.result->error); return; }
                attempt_ = {};
                acceptResult(std::move(output.result)); refreshCache();
            });
        });
}
void DanmakuLibrary::acceptResult(std::shared_ptr<OnlineDanmakuResult> result) {
    if (!result || result->cancelled) return;
    if (!result->error.isEmpty() || result->items.empty()) { fail(result->error.isEmpty() ? QStringLiteral("没有可播放的弹幕") : result->error); return; }
    busy_ = false; progress_ = -1;
    status_ = QStringLiteral("已载入 %1 条，无效 %2 条，不支持模式 %3 条，去重 %4 条")
        .arg(result->items.size()).arg(result->invalid).arg(result->unsupported).arg(result->duplicates);
    if (!result->warning.isEmpty()) { status_ += QStringLiteral("；已载入但缓存失败"); error_ = result->warning; }
    server_ = result->metadata.value("server").toString();
    ready_ = std::move(result);
    emit sourceReady(); emit changed();
}
std::shared_ptr<OnlineDanmakuResult> DanmakuLibrary::takeReadyResult() { return std::exchange(ready_, {}); }
void DanmakuLibrary::loadCached(const QString& key) {
    begin(QStringLiteral("正在载入缓存…"), true);
    work([directory = directory_, key](std::stop_token stop) {
        Work output; output.result = std::make_shared<OnlineDanmakuResult>(readCachedDanmaku(directory, key, stop)); return output;
    }, [this](Work output) { acceptResult(std::move(output.result)); });
}
void DanmakuLibrary::removeCached(const QString& key) {
    begin(QStringLiteral("正在删除缓存…"));
    work([directory = directory_, key](std::stop_token stop) {
        Work output;
        if (!stop.stop_requested()) output.error = deleteCachedDanmaku(directory, key);
        return output;
    }, [this](Work output) {
        if (!output.error.isEmpty()) fail(output.error);
        else { busy_ = false; status_ = QStringLiteral("缓存已删除；已载入的弹幕不受影响"); emit changed(); }
        refreshCache();
    });
}
void DanmakuLibrary::refreshCache() {
    ++scanGeneration_; scannerStop_.request_stop(); scanner_.clear(); scannerStop_ = std::stop_source{};
    const auto generation = scanGeneration_;
    scanning_ = true; emit cacheChanged();
    pendingScan_ = std::make_shared<QVariantList>();
    scanner_.start(QRunnable::create([this, directory = directory_, generation, pending = pendingScan_, stop = scannerStop_.get_token()] {
        *pending = listCachedDanmaku(directory, stop);
        if (stop.stop_requested()) return;
        QMetaObject::invokeMethod(this, [this, generation, weak = std::weak_ptr<QVariantList>(pending)] {
            if (generation != scanGeneration_) return;
            const auto result = weak.lock(); if (!result) return;
            entries_ = std::move(*result); pendingScan_.reset(); scanning_ = false; emit cacheChanged();
        }, Qt::QueuedConnection);
    }));
}
