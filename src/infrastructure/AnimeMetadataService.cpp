#include "infrastructure/AnimeMetadataService.h"
#include <QDate>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QSaveFile>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr qint64 kMaxResponseBytes = 16LL * 1024 * 1024;
constexpr qint64 kCalendarMaxAgeSeconds = 6 * 60 * 60;

void boundReply(QNetworkReply* reply, int timeoutMs) {
    reply->setReadBufferSize(kMaxResponseBytes + 1);
    QObject::connect(reply, &QIODevice::readyRead, reply, [reply] {
        if (reply->bytesAvailable() > kMaxResponseBytes) reply->abort();
    });
    QTimer::singleShot(timeoutMs, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
}

QString normalizeCoverUrl(QString url) {
    if (url.startsWith(QStringLiteral("http://lain.bgm.tv"))) {
        url.replace(0, 4, QStringLiteral("https"));
    }
    return url;
}
} // namespace

AnimeMetadataService::AnimeMetadataService(QString cacheRoot, QObject* parent, int timeoutMs)
    : QObject(parent)
    , cacheDir_(QDir(cacheRoot).filePath(QStringLiteral("metadata")))
    , network_(this)
    , timeoutMs_(timeoutMs)
{
    worker_.setMaxThreadCount(1);
    deadline_.setSingleShot(true);
    connect(&deadline_, &QTimer::timeout, this, [this] {
        if (calendarReply_) {
            auto* reply = calendarReply_.data();
            calendarReply_ = nullptr;
            disconnect(reply, nullptr, this, nullptr);
            reply->abort();
            reply->deleteLater();
            loading_ = false;
            error_ = QStringLiteral("请求追番日历超时");
            emit stateChanged();
        }
    });

    todayWeekday_ = QDate::currentDate().dayOfWeek();
    if (todayWeekday_ < 1 || todayWeekday_ > 7) todayWeekday_ = 1;

    // Load disk cache first so data is instantly available without white screen
    loadCalendarFromDiskCache();
    refreshTimer_.setInterval(60000);
    connect(&refreshTimer_, &QTimer::timeout, this, [this] { refreshCalendar(false); });
}

AnimeMetadataService::~AnimeMetadataService() {
    ++generation_;
    searchStop_.request_stop();
    deadline_.stop();
    if (calendarReply_) {
        auto* r = calendarReply_.data();
        calendarReply_ = nullptr;
        disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }
    if (detailReply_) {
        auto* r = detailReply_.data();
        detailReply_ = nullptr;
        disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }
    if (epReply_) {
        auto* r = epReply_.data();
        epReply_ = nullptr;
        disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }
    if (searchReply_) {
        auto* r = searchReply_.data();
        searchReply_ = nullptr;
        disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }
    if (heroReply_) {
        auto* r = heroReply_.data();
        heroReply_ = nullptr;
        disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }
    workerStop_.request_stop();
    worker_.clear();
    worker_.waitForDone();
}

void AnimeMetadataService::setBaseUrl(const QString& url) {
    baseUrl_ = url;
    while (baseUrl_.endsWith(QLatin1Char('/'))) {
        baseUrl_.chop(1);
    }
}

void AnimeMetadataService::loadCalendarFromDiskCache() {
    QFile file(QDir(cacheDir_).filePath(QStringLiteral("calendar.json")));
    if (!file.open(QIODevice::ReadOnly)) return;
    if (file.size() > kMaxResponseBytes) return;
    const auto bytes = file.readAll();
    file.close();
    if (bytes.isEmpty()) return;

    auto parsed = parseCalendarJson(bytes, todayWeekday_);
    if (parsed.error.isEmpty() && !parsed.weeklyCalendar.isEmpty()) {
        weeklyCalendar_ = std::move(parsed.weeklyCalendar);
        airingToday_ = std::move(parsed.airingToday);
        topRated_ = std::move(parsed.topRated);
        heroAnime_ = std::move(parsed.heroAnime);
        calendarFetchedAt_ = QFileInfo(file).lastModified().toUTC();
        emit calendarChanged();
    }
}

void AnimeMetadataService::saveCalendarToDiskCache(const QByteArray& data) {
    QDir dir(cacheDir_);
    if (!dir.exists()) dir.mkpath(QStringLiteral("."));
    QSaveFile file(dir.filePath(QStringLiteral("calendar.json")));
    if (file.open(QIODevice::WriteOnly)) {
        if (file.write(data) == data.size()) file.commit();
    }
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

void AnimeMetadataService::refreshCalendar(bool force) {
    if (!refreshTimer_.isActive()) refreshTimer_.start();
    if (loading_ && !force) return;

    const auto now = QDateTime::currentDateTimeUtc();
    const bool sameDay = todayWeekday_ == QDate::currentDate().dayOfWeek();
    if (!force && sameDay && !weeklyCalendar_.isEmpty() && calendarFetchedAt_.isValid() &&
        calendarFetchedAt_.secsTo(now) >= 0 && calendarFetchedAt_.secsTo(now) < kCalendarMaxAgeSeconds) {
        if (heroAnime_.value("summary").toString().isEmpty() && !heroReply_) fetchHeroDetails();
        return;
    }

    todayWeekday_ = QDate::currentDate().dayOfWeek();
    if (todayWeekday_ < 1 || todayWeekday_ > 7) todayWeekday_ = 1;

    if (!sameDay) loadCalendarFromDiskCache();
    if (calendarReply_) {
        auto* r = calendarReply_.data();
        calendarReply_ = nullptr;
        disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }

    const quint64 gen = ++generation_;
    workerStop_.request_stop();
    worker_.clear();
    loading_ = true;
    error_.clear();
    emit stateChanged();

    QUrl url(baseUrl_ + QStringLiteral("/calendar"));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DanmaX/0.2.4 (https://github.com/DanmaX)"));
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::SameOriginRedirectPolicy);
    req.setTransferTimeout(timeoutMs_);

    auto* reply = network_.get(req);
    boundReply(reply, timeoutMs_);
    calendarReply_ = reply;
    deadline_.start(timeoutMs_);

    connect(reply, &QNetworkReply::finished, this, [this, reply, gen] {
        if (gen != generation_) {
            reply->deleteLater();
            return;
        }
        deadline_.stop();
        calendarReply_ = nullptr;

        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto error = reply->error();
        const auto body = reply->isOpen() ? reply->readAll() : QByteArray{};
        reply->deleteLater();

        if (error != QNetworkReply::NoError || status != 200 || body.size() > kMaxResponseBytes) {
            loading_ = false;
            error_ = QStringLiteral("获取放送表失败（HTTP %1）").arg(status);
            emit stateChanged();
            return;
        }

        workerStop_.request_stop();
        worker_.clear();
        workerStop_ = std::stop_source{};

        worker_.start([this, body, gen, weekday = todayWeekday_, stop = workerStop_.get_token()] {
            auto parsed = parseCalendarJson(body, weekday, stop);
            if (stop.stop_requested()) return;

            QMetaObject::invokeMethod(this, [this, gen, body, parsed = std::move(parsed)]() mutable {
                if (gen != generation_) return;
                loading_ = false;
                if (!parsed.error.isEmpty()) {
                    error_ = parsed.error;
                } else {
                    saveCalendarToDiskCache(body);
                    calendarFetchedAt_ = QDateTime::currentDateTimeUtc();
                    error_.clear();
                    weeklyCalendar_ = std::move(parsed.weeklyCalendar);
                    airingToday_ = std::move(parsed.airingToday);
                    topRated_ = std::move(parsed.topRated);
                    heroAnime_ = std::move(parsed.heroAnime);
                    emit calendarChanged();
                    fetchHeroDetails();
                }
                emit stateChanged();
            }, Qt::QueuedConnection);
        });
    });
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

void AnimeMetadataService::fetchDetail(qint64 subjectId) {
    clearDetail();
    if (subjectId <= 0) return;
    const auto generation = detailGeneration_;
    loadingDetail_ = true;
    emit detailChanged();
    QNetworkRequest request(QUrl(baseUrl_ + "/v0/subjects/" + QString::number(subjectId)));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DanmaX/0.2.4"));
    request.setTransferTimeout(timeoutMs_);
    auto* reply = network_.get(request);
    boundReply(reply, timeoutMs_);
    detailReply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, subjectId, generation] {
        if (generation != detailGeneration_) { reply->deleteLater(); return; }
        detailReply_ = nullptr;
        const auto body = reply->isOpen() ? reply->readAll() : QByteArray{};
        const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status == 200 && body.size() <= kMaxResponseBytes;
        reply->deleteLater();
        auto detail = ok ? parseSubjectDetail(body) : QVariantMap{};
        if (detail.value("id").toLongLong() != subjectId || detail.value("title").toString().isEmpty()) {
            detailError_ = QStringLiteral("获取番剧详情失败（HTTP %1），请重试").arg(status);
            loadingDetail_ = false;
            emit detailChanged();
            return;
        }
        currentDetail_ = std::move(detail);
        emit detailChanged();
        fetchEpisodes(subjectId, generation, body);
    });
}

void AnimeMetadataService::fetchEpisodes(qint64 subjectId, quint64 generation, QByteArray subject, QJsonArray episodes) {
    if (generation != detailGeneration_) return;
    QUrl url(baseUrl_ + "/v0/episodes");
    QUrlQuery query;
    query.addQueryItem("subject_id", QString::number(subjectId));
    query.addQueryItem("type", "0");
    query.addQueryItem("limit", "100");
    query.addQueryItem("offset", QString::number(episodes.size()));
    url.setQuery(query);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DanmaX/0.2.4"));
    request.setTransferTimeout(timeoutMs_);
    auto* reply = network_.get(request);
    boundReply(reply, timeoutMs_);
    epReply_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, subjectId, generation, subject = std::move(subject), episodes = std::move(episodes)]() mutable {
        if (generation != detailGeneration_) { reply->deleteLater(); return; }
        epReply_ = nullptr;
        const auto body = reply->isOpen() ? reply->readAll() : QByteArray{};
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const bool ok = reply->error() == QNetworkReply::NoError && status == 200 && body.size() <= kMaxResponseBytes;
        reply->deleteLater();
        const auto document = QJsonDocument::fromJson(body);
        const auto page = document.object().value("data");
        if (!ok || !document.isObject() || !page.isArray() || page.toArray().size() > 100) {
            detailError_ = QStringLiteral("获取分集失败（HTTP %1），可重试或手动搜索弹幕").arg(status);
            loadingDetail_ = false;
            emit detailChanged();
            return;
        }
        for (const auto& episode : page.toArray()) episodes.append(episode);
        currentDetail_ = parseSubjectDetail(subject, QJsonDocument(QJsonObject{{"data", episodes}}).toJson(QJsonDocument::Compact));
        const int total = document.object().value("total").toInt(static_cast<int>(episodes.size()));
        if (episodes.size() < total && !page.toArray().isEmpty()) {
            if (episodes.size() >= 5000 || total > 5000) {
                detailError_ = QStringLiteral("分集超过 5000 条上限，请手动选择弹幕");
            } else {
                emit detailChanged();
                fetchEpisodes(subjectId, generation, std::move(subject), std::move(episodes));
                return;
            }
        }
        loadingDetail_ = false;
        emit detailChanged();
    });
}

void AnimeMetadataService::clearDetail() {
    ++detailGeneration_;
    for (auto* pointer : {&detailReply_, &epReply_}) {
        if (!*pointer) continue;
        auto* reply = pointer->data();
        *pointer = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    currentDetail_.clear();
    detailError_.clear();
    loadingDetail_ = false;
    emit detailChanged();
}
void AnimeMetadataService::clearSearch() {
    ++searchGeneration_;
    searchStop_.request_stop();
    if (searchReply_) {
        auto* reply = searchReply_.data();
        searchReply_ = nullptr;
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
    searching_ = false;
    searchResults_.clear();
    searchKeyword_.clear();
    searchError_.clear();
    searchTotal_ = 0;
    emit searchChanged();
}

void AnimeMetadataService::searchBangumi(const QString& keyword) {
    clearSearch();
    const auto trimmed = keyword.trimmed();
    if (trimmed.isEmpty()) return;
    searchKeyword_ = trimmed;
    if (trimmed.size() > 512) {
        searchError_ = QStringLiteral("搜索词不能超过 512 个字符");
        emit searchChanged();
        return;
    }
    searching_ = true;
    searchStop_ = std::stop_source{};
    const auto generation = searchGeneration_;
    emit searchChanged();
    // Bounded first page. A broad match asks the user to refine the keyword.
    QNetworkRequest req(QUrl(baseUrl_ + QStringLiteral("/v0/search/subjects?limit=20&offset=0")));
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DanmaX/0.2.4 (https://github.com/DanmaX)"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(timeoutMs_);
    const auto payload = QJsonDocument(QJsonObject{{"keyword", trimmed}, {"sort", "match"},
        {"filter", QJsonObject{{"type", QJsonArray{2}}, {"nsfw", false}}}}).toJson(QJsonDocument::Compact);
    auto* reply = network_.post(req, payload);
    searchReply_ = reply;
    boundReply(reply, timeoutMs_);

    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        if (generation != searchGeneration_) { reply->deleteLater(); return; }
        searchReply_ = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body = reply->isOpen() ? reply->readAll() : QByteArray{};
        const bool ok = reply->error() == QNetworkReply::NoError && status == 200 && body.size() <= kMaxResponseBytes;
        reply->deleteLater();
        if (!ok) {
            searching_ = false;
            searchError_ = status > 0 ? QStringLiteral("搜索失败（HTTP %1），请重试").arg(status)
                : QStringLiteral("搜索请求失败或超时，请检查网络后重试");
            emit searchChanged();
            return;
        }
        worker_.start([this, body, generation, stop = searchStop_.get_token()] {
            if (stop.stop_requested()) return;
            QVariantList results;
            QString error;
            int total = 0;
            const auto doc = QJsonDocument::fromJson(body);
            const auto data = doc.object().value("data");
            const auto count = doc.object().value("total");
            if (!doc.isObject() || !data.isArray() || data.toArray().size() > 20 || !count.isDouble()
                || count.toDouble() < data.toArray().size() || count.toDouble() != std::floor(count.toDouble())
                || count.toDouble() > std::numeric_limits<int>::max()) {
                error = QStringLiteral("搜索服务返回的数据无效，请重试");
            } else {
                total = count.toInt();
                QSet<qint64> seen;
                for (const auto& val : data.toArray()) {
                    if (stop.stop_requested()) return;
                    const auto obj = val.toObject();
                    const qint64 id = obj.value(QStringLiteral("id")).toInteger();
                    if (id <= 0 || seen.contains(id) || obj.value("type").toInt() != 2 || obj.value("nsfw").toBool()) continue;
                    const auto name = obj.value("name").toString().trimmed();
                    auto title = obj.value("name_cn").toString().trimmed();
                    if (title.isEmpty()) title = name;
                    if (title.isEmpty()) continue;
                    seen.insert(id);

                    const auto imgObj = obj.value(QStringLiteral("images")).toObject();
                    QString cover = imgObj.value(QStringLiteral("large")).toString();
                    if (cover.isEmpty()) cover = imgObj.value(QStringLiteral("common")).toString();
                    cover = normalizeCoverUrl(cover);

                    QVariantMap m;
                    m[QStringLiteral("id")] = id;
                    m[QStringLiteral("title")] = title;
                    m[QStringLiteral("cover")] = cover;
                    m[QStringLiteral("originalTitle")] = name;
                    m[QStringLiteral("airDate")] = obj.value(QStringLiteral("date")).toString();
                    const double score = obj.value("rating").toObject().value("score").toDouble();
                    m[QStringLiteral("score")] = score > 0 && std::isfinite(score) ? QString::number(score, 'f', 1) : QString();
                    results.append(m);
                }
                if (!data.toArray().isEmpty() && results.isEmpty()) error = QStringLiteral("搜索结果没有可用的番剧信息，请调整搜索词");
            }
            if (stop.stop_requested()) return;
            QMetaObject::invokeMethod(this, [this, generation, results = std::move(results), error, total] {
                if (generation != searchGeneration_) return;
                searchResults_ = results;
                searchTotal_ = total;
                searchError_ = error;
                searching_ = false;
                emit searchChanged();
            }, Qt::QueuedConnection);
        });
    });
}
void AnimeMetadataService::fetchHeroDetails() {
    const qint64 heroId = heroAnime_.value(QStringLiteral("id")).toLongLong();
    if (heroId <= 0) return;

    if (heroReply_) {
        auto* r = heroReply_.data();
        heroReply_ = nullptr;
        disconnect(r, nullptr, this, nullptr);
        r->abort();
        r->deleteLater();
    }

    QUrl url(baseUrl_ + QStringLiteral("/v0/subjects/") + QString::number(heroId));
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DanmaX/0.2.4 (https://github.com/DanmaX)"));
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(timeoutMs_);

    auto* reply = network_.get(req);
    heroReply_ = reply;
    boundReply(reply, timeoutMs_);
    connect(reply, &QNetworkReply::finished, this, [this, reply, heroId] {
        heroReply_ = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto body = reply->isOpen() ? reply->readAll() : QByteArray{};
        reply->deleteLater();

        if (reply->error() == QNetworkReply::NoError && status == 200 && body.size() <= kMaxResponseBytes && !body.isEmpty()) {
            const auto doc = QJsonDocument::fromJson(body);
            if (doc.isObject()) {
                const auto obj = doc.object();
                const QString summary = obj.value(QStringLiteral("summary")).toString().trimmed();
                QStringList tags;
                for (const auto& tagVal : obj.value(QStringLiteral("tags")).toArray()) {
                    const QString tag = tagVal.toObject().value(QStringLiteral("name")).toString().trimmed();
                    if (!tag.isEmpty() && tags.size() < 5) tags.append(tag);
                }
                if (heroAnime_.value(QStringLiteral("id")).toLongLong() == heroId) {
                    if (!summary.isEmpty()) heroAnime_[QStringLiteral("summary")] = summary;
                    if (!tags.isEmpty()) heroAnime_[QStringLiteral("tags")] = tags;
                    emit calendarChanged();
                }
            }
        }
    });
}
