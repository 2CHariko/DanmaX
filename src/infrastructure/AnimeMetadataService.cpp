#include "infrastructure/AnimeMetadataService.h"
#include "version.h"
#include "infrastructure/CoverCache.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr qint64 maxResponse = 16LL * 1024 * 1024;
QString normalizeCoverUrl(QString url) {
    if (url.startsWith("http://lain.bgm.tv")) url.replace(0, 4, "https");
    return url;
}
}
AnimeMetadataService::AnimeMetadataService(QString root, QObject* parent, int timeoutMs)
    : AnimeMetadataService(std::move(root), MetadataCache::Policy{}, parent, timeoutMs) {}
AnimeMetadataService::AnimeMetadataService(QString root, MetadataCache::Policy policy, QObject* parent, int timeoutMs)
    : QObject(parent), cache_(std::move(root), std::move(policy)), network_(this), timeoutMs_(timeoutMs) {
    covers_ = new CoverCache(&cache_, this, timeoutMs);
    worker_.setMaxThreadCount(1);
    todayWeekday_ = cache_.now().toLocalTime().date().dayOfWeek();
    refreshTimer_.setInterval(60000);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] { refreshCalendar(false); });
    connect(&cache_, &MetadataCache::usageChanged, this, [this](qint64 json, qint64 covers) {
        jsonBytes_ = json; coverBytes_ = covers; emit cacheChanged();
    });
    // Loading is asynchronous and source-aware; the home page initiates the first access.
}
AnimeMetadataService::~AnimeMetadataService() {
    cancelAll(); worker_.clear(); worker_.waitForDone();
    delete covers_; covers_ = nullptr;
}
QObject* AnimeMetadataService::coverCache() { return covers_; }
QString AnimeMetadataService::resourceKey(Kind kind, const QString& identity) const {
    return baseUrl_ + '|' + QString::number(static_cast<int>(kind)) + '|'
        + (kind == Kind::Search ? QStringLiteral("/v0/search/subjects?limit=20&offset=0|") : QString()) + identity;
}
void AnimeMetadataService::setBaseUrl(const QString& url) {
    auto normalized = url;
    while (normalized.endsWith('/')) normalized.chop(1);
    if (normalized == baseUrl_) return;
    cancelAll(); baseUrl_ = normalized; failedAt_.clear();
    weeklyCalendar_.clear(); airingToday_.clear(); topRated_.clear(); heroAnime_.clear();
    loading_ = false; error_.clear(); calendarStale_ = false; calendarDate_ = {};
    calendarFetchedAt_ = {}; heroFetchedAt_ = {};
    clearSearch(); clearDetail();
    cache_.setPins("calendar", {}); cache_.setPins("hero", {});
    emit calendarChanged(); emit stateChanged();
}
void AnimeMetadataService::cancelAll() {
    refreshTimer_.stop();
    for (const auto& job : std::as_const(jobs_)) {
        job->active = false;
        if (auto* reply = job->reply.data()) { job->reply = nullptr; reply->abort(); reply->deleteLater(); }
    }
    jobs_.clear(); ++calendarGeneration_; ++heroGeneration_; ++detailGeneration_; ++searchGeneration_;
}
void AnimeMetadataService::unsubscribe(const QString& scope) {
    const auto jobs = jobs_.values();
    for (const auto& job : jobs) {
        job->watches.removeIf([&](const Watch& watch) { return watch.scope == scope; });
        if (!job->watches.isEmpty()) continue;
        job->active = false; jobs_.remove(job->key);
        if (auto* reply = job->reply.data()) { job->reply = nullptr; reply->abort(); reply->deleteLater(); }
    }
}
void AnimeMetadataService::publish(const std::shared_ptr<Job>& job, Result result, bool done) {
    if (!job->active) return;
    const auto watches = job->watches;
    job->cached = result;
    if (done) { job->active = false; jobs_.remove(job->key); }
    for (const auto& watch : watches) watch.callback(result);
}
void AnimeMetadataService::request(Kind kind, QString identity, QString scope, bool force, std::function<void(Result)> callback) {
    if (clearing_) return;
    const auto key = resourceKey(kind, identity);
    if (auto found = jobs_.value(key)) {
        // Explicit refresh bypasses a pending disk lookup as well as a fresh hit.
        found->force |= force;
        found->watches.append({std::move(scope), callback});
        if (found->cached.available) callback(found->cached);
        return;
    }
    auto job = std::make_shared<Job>();
    job->key = key; job->identity = std::move(identity); job->kind = kind; job->force = force;
    job->ttl = kind == Kind::Subject ? 86400 : kind == Kind::Search ? 3600 : 21600;
    job->watches.append({std::move(scope), std::move(callback)});
    jobs_.insert(key, job);
    cache_.read(key, false, [this, job](MetadataCache::Record record) {
        if (!job->active) return;
        if (!record.valid && job->kind == Kind::Calendar && baseUrl_ == "https://api.bgm.tv") {
            cache_.importCalendar([this, job](MetadataCache::Record legacy) { cached(job, std::move(legacy), true); });
        } else cached(job, std::move(record));
    });
}
void AnimeMetadataService::cached(const std::shared_ptr<Job>& job, MetadataCache::Record record, bool legacy) {
    if (!job->active) return;
    const auto weekday = cache_.now().toLocalTime().date().dayOfWeek();
    worker_.start([this, job, record = std::move(record), legacy, weekday] {
        auto result = record.valid ? parseResource(job->kind, job->identity, record.data, weekday) : Result{};
        QMetaObject::invokeMethod(this, [this, job, result = std::move(result), record, legacy]() mutable {
            if (!job->active) return;
            const auto age = failedAt_.value(job->key).secsTo(cache_.now());
            const bool cooldown = !job->force && failedAt_.contains(job->key) && age >= 0 && age < 300;
            const bool fresh = cache_.fresh(record, job->ttl);
            const bool fetch = job->force || ((!result.available || !fresh) && !cooldown);
            if (result.available) {
                result.fetchedAt = record.fetchedAt;
                // Resume cleanup if the process exited after migration committed but before its callback.
                if (!legacy && job->kind == Kind::Calendar && baseUrl_ == "https://api.bgm.tv") cache_.finishCalendarMigration();
                result.stale = !fresh; result.refreshing = fetch;
                if (cooldown && !fresh) result.error = QStringLiteral("更新失败，继续显示本地缓存；稍后自动重试");
                if (legacy) cache_.write(job->key, false, record.data, record.fetchedAt, [this, key = job->key](MetadataCache::Record) {
                    // Verify the new entry exists before deleting the legacy file.
                    cache_.read(key, false, [this](MetadataCache::Record saved) { if (saved.valid) cache_.finishCalendarMigration(); });
                });
                publish(job, result, !fetch);
            } else if (!fetch) {
                result.error = QStringLiteral("请求失败，请稍后重试或手动刷新"); publish(job, result, true);
            }
            if (fetch) network(job);
        }, Qt::QueuedConnection);
    });
}
void AnimeMetadataService::network(const std::shared_ptr<Job>& job) {
    if (!job->active) return;
    QString path;
    if (job->kind == Kind::Calendar) path = "/calendar";
    else if (job->kind == Kind::Subject) path = "/v0/subjects/" + job->identity;
    else if (job->kind == Kind::Search) path = "/v0/search/subjects?limit=20&offset=0";
    else path = "/v0/episodes?subject_id=" + job->identity + "&type=0&limit=100&offset=" + QString::number(job->episodes.size());
    QNetworkRequest req(QUrl(baseUrl_ + path));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral(DANMAX_HTTP_USER_AGENT));
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(timeoutMs_);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    QNetworkReply* reply;
    if (job->kind == Kind::Search) {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = network_.post(req, job->identity.toUtf8());
    } else reply = network_.get(req);
    job->reply = reply;
    reply->setReadBufferSize(maxResponse + 1);
    connect(reply, &QIODevice::readyRead, reply, [reply] { if (reply->bytesAvailable() > maxResponse) reply->abort(); });
    QTimer::singleShot(timeoutMs_, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, job, reply] {
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status == 200;
        const auto body = reply->isOpen() ? reply->readAll() : QByteArray{};
        reply->deleteLater(); job->reply = nullptr;
        if (!job->active) return;
        if (!ok || body.size() > maxResponse) {
            failedAt_[job->key] = cache_.now();
            auto result = job->cached; result.refreshing = false;
            result.error = result.available ? QStringLiteral("更新失败，继续显示本地缓存（HTTP %1）").arg(status)
                : QStringLiteral("请求失败或超时（HTTP %1），请重试").arg(status);
            publish(job, result, true); return;
        }
        parseNetwork(job, body);
    });
}
void AnimeMetadataService::parseNetwork(const std::shared_ptr<Job>& job, QByteArray body) {
    const auto weekday = cache_.now().toLocalTime().date().dayOfWeek();
    worker_.start([this, job, body = std::move(body), weekday]() mutable {
        auto result = parseResource(job->kind, job->identity, body, weekday, false);
        bool more = false;
        if (job->kind == Kind::Episodes && result.available) {
            const auto page = QJsonDocument::fromJson(body).object();
            const auto items = page.value("data").toArray();
            const int total = page.value("total").toInt();
            if ((job->expectedTotal >= 0 && total != job->expectedTotal) || items.size() > 100
                || job->episodes.size() + items.size() > total || (items.isEmpty() && job->episodes.size() < total)) result.available = false;
            job->expectedTotal = total;
            for (const auto& item : items) {
                const auto id = item.toObject().value("id").toInteger();
                if (job->episodeIds.contains(id)) result.available = false;
                job->episodeIds.insert(id);
            }
            if (result.available) {
                for (const auto& item : items) job->episodes.append(item);
                more = job->episodes.size() < total;
                body = QJsonDocument(QJsonObject{{"data", job->episodes}, {"total", more ? static_cast<int>(job->episodes.size()) : total}}).toJson(QJsonDocument::Compact);
                result = parseResource(Kind::Episodes, job->identity, body, weekday);
            }
        }
        QMetaObject::invokeMethod(this, [this, job, body = std::move(body), result = std::move(result), more]() mutable {
            if (!job->active) return;
            if (more && result.available) {
                // Partial pages are session-only and cannot replace a cached complete list.
                if (!job->cached.available) {
                    result.refreshing = true;
                    const auto watches = job->watches;
                    for (const auto& watch : watches) watch.callback(result);
                }
                network(job); return;
            }
            if (!result.available) {
                failedAt_[job->key] = cache_.now();
                auto failure = job->cached; failure.refreshing = false;
                failure.error = failure.available ? QStringLiteral("返回数据无效，继续显示本地缓存") : QStringLiteral("服务返回数据无效，请重试");
                publish(job, failure, true); return;
            }
            failedAt_.remove(job->key);
            result.fetchedAt = cache_.now();
            cache_.write(job->key, false, body, result.fetchedAt);
            publish(job, result, true);
        }, Qt::QueuedConnection);
    });
}
AnimeMetadataService::Result AnimeMetadataService::parseResource(Kind kind, const QString& identity, const QByteArray& data, int weekday, bool completeEpisodes) {
    Result result;
    if (data.size() > maxResponse) return result;
    if (kind == Kind::Calendar) {
        const auto parsed = parseCalendarJson(data, weekday);
        if (!parsed.error.isEmpty()) return result;
        result.value = QVariantMap{{"weekly", parsed.weeklyCalendar}, {"today", parsed.airingToday}, {"top", parsed.topRated}, {"hero", parsed.heroAnime}};
    } else if (kind == Kind::Subject) {
        const auto parsed = parseSubjectDetail(data);
        if (parsed.value("id").toLongLong() != identity.toLongLong() || parsed.value("title").toString().trimmed().isEmpty()) return result;
        result.value = parsed;
    } else if (kind == Kind::Episodes) {
        const auto doc = QJsonDocument::fromJson(data);
        const auto obj = doc.object();
        const auto totalValue = obj.value("total");
        if (!doc.isObject() || !obj.value("data").isArray() || !totalValue.isDouble() || totalValue.toDouble() < 0
            || totalValue.toDouble() != std::floor(totalValue.toDouble()) || totalValue.toDouble() > 5000) return result;
        const auto values = obj.value("data").toArray();
        if (values.size() > totalValue.toInt() || (completeEpisodes && values.size() != totalValue.toInt())) return result;
        QVariantList episodes; QSet<qint64> ids;
        for (const auto& value : values) {
            const auto ep = value.toObject();
            const auto id = ep.value("id").toInteger();
            const auto sort = ep.value("sort").toDouble(-1);
            if (id <= 0 || ids.contains(id) || ep.value("type").toInt(-1) != 0 || !std::isfinite(sort) || sort < 0) return result;
            ids.insert(id);
            auto title = ep.value("name_cn").toString().trimmed();
            if (title.isEmpty()) title = ep.value("name").toString().trimmed();
            if (title.isEmpty()) title = QStringLiteral("第 %1 话").arg(sort);
            episodes.append(QVariantMap{{"id", id}, {"sort", sort}, {"title", title}, {"airDate", ep.value("airdate").toString()}});
        }
        result.value = episodes;
    } else {
        const auto doc = QJsonDocument::fromJson(data);
        const auto values = doc.object().value("data");
        const auto total = doc.object().value("total");
        if (!doc.isObject() || !values.isArray() || values.toArray().size() > 20 || !total.isDouble()
            || total.toDouble() < values.toArray().size() || total.toDouble() != std::floor(total.toDouble())
            || total.toDouble() > std::numeric_limits<int>::max()) return result;
        QVariantList items; QSet<qint64> ids;
        for (const auto& value : values.toArray()) {
            const auto obj = value.toObject(); const auto id = obj.value("id").toInteger();
            if (id <= 0 || ids.contains(id) || obj.value("type").toInt() != 2 || obj.value("nsfw").toBool()) continue;
            auto title = obj.value("name_cn").toString().trimmed();
            if (title.isEmpty()) title = obj.value("name").toString().trimmed();
            if (title.isEmpty()) continue;
            ids.insert(id);
            auto cover = obj.value("images").toObject().value("large").toString();
            if (cover.isEmpty()) cover = obj.value("images").toObject().value("common").toString();
            const auto score = obj.value("rating").toObject().value("score").toDouble();
            items.append(QVariantMap{{"id", id}, {"title", title}, {"originalTitle", obj.value("name").toString()},
                {"airDate", obj.value("date").toString()}, {"cover", normalizeCoverUrl(cover)},
                {"score", score > 0 && std::isfinite(score) ? QString::number(score, 'f', 1) : QString()}});
        }
        if (!values.toArray().isEmpty() && items.isEmpty()) return result;
        result.value = QVariantMap{{"items", items}, {"total", total.toInt()}};
    }
    result.available = true; return result;
}
void AnimeMetadataService::applyCalendar(QVariantMap value) {
    const auto oldId = heroAnime_.value("id");
    const auto oldHero = heroAnime_;
    const auto oldWeekly = weeklyCalendar_, oldToday = airingToday_, oldTop = topRated_;
    const auto oldWeekday = todayWeekday_;
    weeklyCalendar_ = value.value("weekly").toList(); airingToday_ = value.value("today").toList();
    topRated_ = value.value("top").toList(); heroAnime_ = value.value("hero").toMap();
    if (oldId == heroAnime_.value("id")) {
        if (oldHero.contains("summary")) heroAnime_["summary"] = oldHero.value("summary");
        if (oldHero.contains("tags")) heroAnime_["tags"] = oldHero.value("tags");
    } else heroFetchedAt_ = {};
    calendarDate_ = cache_.now().toLocalTime().date(); todayWeekday_ = calendarDate_.dayOfWeek();
    if (weeklyCalendar_ != oldWeekly || airingToday_ != oldToday || topRated_ != oldTop
        || heroAnime_ != oldHero || todayWeekday_ != oldWeekday) emit calendarChanged();
}
void AnimeMetadataService::refreshCalendar(bool force) {
    if (clearing_ || (loading_ && !force)) return;
    // Re-entering the page must not restart the timer or reload a fresh snapshot.
    if (!refreshTimer_.isActive()) refreshTimer_.start();
    const auto now = cache_.now();
    const auto failed = failedAt_.value(resourceKey(Kind::Calendar, {}));
    const auto failureAge = failed.secsTo(now);
    const bool cooldown = failed.isValid() && failureAge >= 0 && failureAge < 300;
    if (!force && calendarFetchedAt_.isValid() && calendarDate_ == now.toLocalTime().date()
        && (cache_.fresh({.fetchedAt = calendarFetchedAt_, .valid = true}, 21600) || cooldown)) {
        if (baseUrl_ == "https://api.bgm.tv") cache_.finishCalendarMigration();
        fetchHeroDetails();
        return;
    }
    unsubscribe("calendar"); const auto gen = ++calendarGeneration_;
    cache_.setPins("calendar", {resourceKey(Kind::Calendar, {})});
    loading_ = true; error_.clear(); emit stateChanged();
    request(Kind::Calendar, {}, "calendar", force, [this, gen, force](Result result) {
        if (gen != calendarGeneration_) return;
        loading_ = result.refreshing; calendarStale_ = result.stale; error_ = result.error;
        if (result.available) {
            calendarFetchedAt_ = result.fetchedAt;
            applyCalendar(result.value.toMap()); fetchHeroDetails(force && !result.refreshing);
        }
        emit stateChanged();
    });
}
void AnimeMetadataService::fetchHeroDetails(bool force) {
    const auto id = heroAnime_.value("id").toLongLong();
    if (!force && cache_.fresh({.fetchedAt = heroFetchedAt_, .valid = heroFetchedAt_.isValid()}, 86400)) return;
    const auto pending = jobs_.value(resourceKey(Kind::Subject, QString::number(id)));
    if (pending && std::any_of(pending->watches.begin(), pending->watches.end(), [](const Watch& watch) { return watch.scope == "hero"; })) {
        pending->force |= force;
        return;
    }
    unsubscribe("hero"); const auto gen = ++heroGeneration_;
    cache_.setPins("hero", id > 0 ? QSet<QString>{resourceKey(Kind::Subject, QString::number(id))} : QSet<QString>{});
    if (id <= 0) return;
    request(Kind::Subject, QString::number(id), "hero", force, [this, gen, id](Result result) {
        if (gen != heroGeneration_ || heroAnime_.value("id").toLongLong() != id || !result.available) return;
        const auto subject = result.value.toMap();
        const auto previous = heroAnime_;
        heroFetchedAt_ = result.fetchedAt;
        heroAnime_["summary"] = subject.value("summary"); heroAnime_["tags"] = subject.value("tags");
        if (heroAnime_ != previous) emit calendarChanged();
    });
}
void AnimeMetadataService::clearDetail() {
    ++detailGeneration_; unsubscribe("detail"); cache_.setPins("detail", {});
    detailId_ = 0; loadingDetail_ = detailSubjectLoading_ = detailEpisodesLoading_ = false;
    episodesAvailable_ = false;
    detailStale_ = detailSubjectStale_ = detailEpisodesStale_ = false;
    detailError_.clear(); subjectError_.clear(); episodesError_.clear(); detailSubject_.clear(); detailEpisodes_.clear(); currentDetail_.clear(); emit detailChanged();
}
void AnimeMetadataService::updateDetail() {
    currentDetail_ = detailSubject_;
    if (!currentDetail_.isEmpty()) currentDetail_["episodes"] = detailEpisodes_;
    loadingDetail_ = detailSubjectLoading_ || detailEpisodesLoading_;
    detailStale_ = detailSubjectStale_ || detailEpisodesStale_;
    detailError_ = !subjectError_.isEmpty() ? subjectError_ : episodesError_;
    emit detailChanged();
}
void AnimeMetadataService::fetchDetail(qint64 id, bool force) {
    if (id <= 0 || clearing_) return;
    if (id != detailId_) clearDetail(); else { ++detailGeneration_; unsubscribe("detail"); }
    detailId_ = id; const auto gen = detailGeneration_;
    detailSubjectLoading_ = detailEpisodesLoading_ = true; subjectError_.clear(); episodesError_.clear(); updateDetail();
    const auto identity = QString::number(id);
    cache_.setPins("detail", {resourceKey(Kind::Subject, identity), resourceKey(Kind::Episodes, identity)});
    request(Kind::Subject, identity, "detail", force, [this, gen](Result result) {
        if (gen != detailGeneration_) return;
        detailSubjectLoading_ = result.refreshing; detailSubjectStale_ = result.stale; subjectError_ = result.error;
        if (result.available) detailSubject_ = result.value.toMap(); updateDetail();
    });
    request(Kind::Episodes, identity, "detail", force, [this, gen](Result result) {
        if (gen != detailGeneration_) return;
        detailEpisodesLoading_ = result.refreshing; detailEpisodesStale_ = result.stale; episodesError_ = result.error;
        if (result.available) { detailEpisodes_ = result.value.toList(); episodesAvailable_ = true; }
        updateDetail();
    });
}
void AnimeMetadataService::clearSearch() {
    ++searchGeneration_; unsubscribe("search"); cache_.setPins("search", {});
    searching_ = searchStale_ = false; searchResults_.clear(); searchKeyword_.clear(); searchError_.clear(); searchTotal_ = 0; emit searchChanged();
}
void AnimeMetadataService::searchBangumi(const QString& keyword, bool force) {
    if (clearing_) return;
    const auto trimmed = keyword.trimmed();
    if (trimmed != searchKeyword_) clearSearch(); else { ++searchGeneration_; unsubscribe("search"); }
    if (trimmed.isEmpty()) return;
    searchKeyword_ = trimmed;
    if (trimmed.size() > 512) { searching_ = false; searchError_ = QStringLiteral("搜索词不能超过 512 个字符"); emit searchChanged(); return; }
    searching_ = true; searchError_.clear(); const auto gen = searchGeneration_; emit searchChanged();
    const auto payload = QJsonDocument(QJsonObject{{"keyword", trimmed}, {"sort", "match"},
        {"filter", QJsonObject{{"type", QJsonArray{2}}, {"nsfw", false}}}}).toJson(QJsonDocument::Compact);
    cache_.setPins("search", {resourceKey(Kind::Search, QString::fromUtf8(payload))});
    request(Kind::Search, QString::fromUtf8(payload), "search", force, [this, gen](Result result) {
        if (gen != searchGeneration_) return;
        searching_ = result.refreshing; searchStale_ = result.stale; searchError_ = result.error;
        if (result.available) { const auto values = result.value.toMap(); searchResults_ = values.value("items").toList(); searchTotal_ = values.value("total").toInt(); }
        emit searchChanged();
    });
}
void AnimeMetadataService::clearMetadataCache() {
    if (clearing_) return;
    clearing_ = true; cacheCleared_ = false; cacheError_.clear(); cancelAll(); failedAt_.clear(); covers_->reset();
    clearSearch(); clearDetail(); weeklyCalendar_.clear(); airingToday_.clear(); topRated_.clear(); heroAnime_.clear();
    calendarDate_ = {}; loading_ = calendarStale_ = false; error_.clear();
    calendarFetchedAt_ = {}; heroFetchedAt_ = {};
    cache_.setPins("calendar", {}); cache_.setPins("hero", {});
    emit calendarChanged(); emit stateChanged(); emit cacheChanged();
    cache_.clear([this](QString error) { clearing_ = false; cacheCleared_ = error.isEmpty(); cacheError_ = error; emit cacheChanged(); });
}

AnimeMetadataService::CalendarParsed AnimeMetadataService::parseCalendarJson(
    const QByteArray& json, int todayDayOfWeek, std::stop_token stop)
{
    CalendarParsed res;
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isArray() || doc.array().isEmpty()) {
        res.error = QStringLiteral("无效的日历 JSON 格式");
        return res;
    }

    QVariantList allItems;
    QSet<int> weekdays;
    const auto rootArray = doc.array();

    for (const auto& dayVal : rootArray) {
        if (stop.stop_requested()) return {};
        const auto dayObj = dayVal.toObject();
        const auto weekdayObj = dayObj.value(QStringLiteral("weekday")).toObject();
        const int weekdayId = weekdayObj.value(QStringLiteral("id")).toInt();
        if (weekdayId < 1 || weekdayId > 7 || weekdays.contains(weekdayId) || !dayObj.value("items").isArray()) {
            res.error = QStringLiteral("放送表星期或条目格式无效");
            return res;
        }
        weekdays.insert(weekdayId);
        const QString weekdayCn = weekdayObj.value(QStringLiteral("cn")).toString();
        const QString weekdayEn = weekdayObj.value(QStringLiteral("en")).toString();

        QVariantList dayItems;
        for (const auto& itemVal : dayObj.value(QStringLiteral("items")).toArray()) {
            if (stop.stop_requested()) return {};
            const auto itemObj = itemVal.toObject();
            const qint64 id = itemObj.value(QStringLiteral("id")).toInteger();
            if (id <= 0) continue;

            const QString name = itemObj.value(QStringLiteral("name")).toString();
            QString nameCn = itemObj.value(QStringLiteral("name_cn")).toString();
            if (nameCn.trimmed().isEmpty()) nameCn = name;
            const QString airDate = itemObj.value(QStringLiteral("air_date")).toString();

            const auto ratingObj = itemObj.value(QStringLiteral("rating")).toObject();
            const double score = ratingObj.value(QStringLiteral("score")).toDouble();
            const int total = ratingObj.value(QStringLiteral("total")).toInt();

            const auto imagesObj = itemObj.value(QStringLiteral("images")).toObject();
            QString cover = imagesObj.value(QStringLiteral("large")).toString();
            if (cover.isEmpty()) cover = imagesObj.value(QStringLiteral("common")).toString();
            if (cover.isEmpty()) cover = imagesObj.value(QStringLiteral("medium")).toString();
            cover = normalizeCoverUrl(cover);

            QVariantMap item;
            item[QStringLiteral("id")] = id;
            item[QStringLiteral("title")] = nameCn;
            item[QStringLiteral("originalTitle")] = name;
            item[QStringLiteral("airDate")] = airDate;
            item[QStringLiteral("score")] = score > 0.0 ? QString::number(score, 'f', 1) : QString();
            item[QStringLiteral("rawScore")] = score;
            item[QStringLiteral("ratingCount")] = total;
            item[QStringLiteral("cover")] = cover;
            item[QStringLiteral("weekday")] = weekdayId;

            dayItems.append(item);
            allItems.append(item);
        }

        QVariantMap dayMap;
        dayMap[QStringLiteral("weekdayId")] = weekdayId;
        dayMap[QStringLiteral("weekdayCn")] = weekdayCn;
        dayMap[QStringLiteral("weekdayEn")] = weekdayEn;
        dayMap[QStringLiteral("items")] = dayItems;
        res.weeklyCalendar.append(dayMap);

        if (weekdayId == todayDayOfWeek) {
            res.airingToday = dayItems;
        }
    }

    // Top rated sorting
    std::sort(allItems.begin(), allItems.end(), [](const QVariant& a, const QVariant& b) {
        return a.toMap().value(QStringLiteral("rawScore")).toDouble() >
               b.toMap().value(QStringLiteral("rawScore")).toDouble();
    });

    const qsizetype topLimit = std::min<qsizetype>(allItems.size(), 20);
    for (qsizetype i = 0; i < topLimit; ++i) {
        if (allItems[i].toMap().value(QStringLiteral("rawScore")).toDouble() > 0.0) {
            res.topRated.append(allItems[i]);
        }
    }

    // Determine Hero Anime: prefer highest rated today, otherwise top rated overall
    if (!res.airingToday.isEmpty()) {
        auto todaySorted = res.airingToday;
        std::sort(todaySorted.begin(), todaySorted.end(), [](const QVariant& a, const QVariant& b) {
            return a.toMap().value(QStringLiteral("rawScore")).toDouble() >
                   b.toMap().value(QStringLiteral("rawScore")).toDouble();
        });
        res.heroAnime = todaySorted.first().toMap();
    } else if (!res.topRated.isEmpty()) {
        res.heroAnime = res.topRated.first().toMap();
    }

    return res;
}

QVariantMap AnimeMetadataService::parseSubjectDetail(
    const QByteArray& json, const QByteArray& episodesJson, std::stop_token stop)
{
    QVariantMap res;
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return res;
    const auto obj = doc.object();

    const qint64 id = obj.value(QStringLiteral("id")).toInteger();
    const QString name = obj.value(QStringLiteral("name")).toString();
    QString nameCn = obj.value(QStringLiteral("name_cn")).toString();
    if (nameCn.trimmed().isEmpty()) nameCn = name;
    const QString summary = obj.value(QStringLiteral("summary")).toString().trimmed();
    const QString airDate = obj.value(QStringLiteral("date")).toString();
    const QString platform = obj.value(QStringLiteral("platform")).toString();
    const int eps = obj.value(QStringLiteral("eps")).toInt();

    const auto imagesObj = obj.value(QStringLiteral("images")).toObject();
    QString cover = imagesObj.value(QStringLiteral("large")).toString();
    if (cover.isEmpty()) cover = imagesObj.value(QStringLiteral("common")).toString();
    cover = normalizeCoverUrl(cover);

    const auto ratingObj = obj.value(QStringLiteral("rating")).toObject();
    const double score = ratingObj.value(QStringLiteral("score")).toDouble();
    const int total = ratingObj.value(QStringLiteral("total")).toInt();

    QStringList tags;
    for (const auto& tagVal : obj.value(QStringLiteral("tags")).toArray()) {
        const auto tagObj = tagVal.toObject();
        const QString tag = tagObj.value(QStringLiteral("name")).toString().trimmed();
        if (!tag.isEmpty() && tags.size() < 6) tags.append(tag);
    }

    res[QStringLiteral("id")] = id;
    res[QStringLiteral("title")] = nameCn;
    res[QStringLiteral("originalTitle")] = name;
    res[QStringLiteral("summary")] = summary;
    res[QStringLiteral("airDate")] = airDate;
    res[QStringLiteral("platform")] = platform;
    res[QStringLiteral("eps")] = eps;
    res[QStringLiteral("cover")] = cover;
    res[QStringLiteral("score")] = score > 0.0 ? QString::number(score, 'f', 1) : QString();
    res[QStringLiteral("ratingCount")] = total;
    res[QStringLiteral("tags")] = tags;

    // Episodes
    QVariantList epsList;
    if (!episodesJson.isEmpty()) {
        const auto epDoc = QJsonDocument::fromJson(episodesJson);
        const auto epArray = epDoc.isObject() ? epDoc.object().value(QStringLiteral("data")).toArray() : epDoc.array();
        for (const auto& val : epArray) {
            if (stop.stop_requested()) break;
            const auto epObj = val.toObject();
            const int epType = epObj.value(QStringLiteral("type")).toInt();
            // type 0 = 本篇
            if (epType != 0 && epObj.contains(QStringLiteral("type"))) continue;

            const qint64 epId = epObj.value(QStringLiteral("id")).toInteger();
            const double sortNum = epObj.value(QStringLiteral("sort")).toDouble();
            QString epName = epObj.value(QStringLiteral("name_cn")).toString().trimmed();
            if (epName.isEmpty()) epName = epObj.value(QStringLiteral("name")).toString().trimmed();
            if (epName.isEmpty()) epName = QStringLiteral("第 %1 话").arg(sortNum > 0 ? QString::number(sortNum) : QString::number(epId));

            QVariantMap ep;
            ep[QStringLiteral("id")] = epId;
            ep[QStringLiteral("sort")] = sortNum;
            ep[QStringLiteral("title")] = epName;
            ep[QStringLiteral("airDate")] = epObj.value(QStringLiteral("airdate")).toString();
            epsList.append(ep);
        }
    }
    res[QStringLiteral("episodes")] = epsList;

    return res;
}
