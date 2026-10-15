#include "infrastructure/CoverCache.h"
#include "version.h"
#include <QBuffer>
#include <QImageReader>
#include <QFileInfo>
#include <QNetworkRequest>
#include <QTimer>

namespace {
constexpr qint64 maxBytes = 8LL * 1024 * 1024;
QByteArray imageFormat(const QByteArray& bytes) {
    QBuffer buffer; buffer.setData(bytes); buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const auto size = reader.size();
    if (size.width() <= 0 || size.height() <= 0 || size.width() > 8192 || size.height() > 8192
        || qint64(size.width()) * size.height() > 16000000) return {};
    const auto format = reader.format();
    return reader.read().isNull() ? QByteArray{} : format;
}
}
CoverRequest::CoverRequest(CoverCache* service, QObject* parent) : QObject(parent), service_(service) {}
CoverRequest::~CoverRequest() { if (service_) service_->detach(this); }
void CoverRequest::setSource(const QUrl& url) { if (source_ != url) { source_ = url; emit sourceChanged(); } }
void CoverRequest::setLoading(bool loading) { if (loading_ != loading) { loading_ = loading; emit loadingChanged(); } }
void CoverRequest::setUrl(const QUrl& url) {
    if (url_ == url) return;
    if (service_) service_->detach(this);
    url_ = url; setLoading(false); setSource({}); emit urlChanged();
    if (service_) service_->attach(this);
}
void CoverRequest::retry() {
    if (!service_) return;
    if (source_.isEmpty()) { service_->detach(this); service_->attach(this); }
    else service_->refresh(this);
}
CoverCache::CoverCache(MetadataCache* store, QObject* parent, int timeoutMs)
    : QObject(parent), store_(store), network_(this), timeoutMs_(timeoutMs) { worker_.setMaxThreadCount(4); }
CoverCache::~CoverCache() { reset(); worker_.clear(); worker_.waitForDone(); }
QObject* CoverCache::createRequest(QObject* owner) {
    if (!owner || owner->thread() != thread()) return nullptr;
    requests_.removeIf([](const auto& request) { return !request; });
    auto* request = new CoverRequest(this, owner); requests_.append(request); return request;
}
void CoverCache::pins() {
    QSet<QString> keys, files;
    for (const auto& entry : std::as_const(entries_)) if (entry->active && !entry->watchers.isEmpty()) {
        keys.insert(entry->key);
        if (entry->cached.localUrl.isLocalFile()) files.insert(QFileInfo(entry->cached.localUrl.toLocalFile()).fileName());
    }
    store_->setPins("covers", keys);
    store_->setCoverFiles(files);
}
void CoverCache::attach(CoverRequest* request) {
    const auto url = request->url();
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") || url.host().isEmpty() || !url.userInfo().isEmpty()) return;
    const auto key = "cover|" + url.toString(QUrl::FullyEncoded);
    if (auto found = entries_.value(key)) {
        found->watchers.append(request); request->setSource(found->cached.localUrl); request->setLoading(!found->finished); pins();
        refresh(request);
        return;
    }
    // Four active transfers plus a bounded pending set; rejected visible items retry on re-entry.
    if (entries_.size() >= 260) return;
    request->setLoading(true);
    auto entry = std::make_shared<Entry>(); entry->key = key; entry->url = url; entry->watchers.append(request);
    entries_.insert(key, entry); pins();
    store_->read(key, true, [this, entry](MetadataCache::Record record) {
        if (!entry->active) return;
        if (!record.valid) { enqueue(entry); return; }
        worker_.start([this, entry, record = std::move(record)]() mutable {
            const bool valid = !imageFormat(record.data).isEmpty();
            record.data.clear();
            QMetaObject::invokeMethod(this, [this, entry, record, valid] {
                if (!entry->active) return;
                if (valid) {
                    entry->cached = record;
                    pins();
                    for (const auto& request : std::as_const(entry->watchers)) if (request) request->setSource(record.localUrl);
                    if (store_->fresh(record, 30LL * 86400)) { entry->finished = true; setLoading(entry, false); return; }
                }
                enqueue(entry);
            }, Qt::QueuedConnection);
        });
    });
}
void CoverCache::refresh(CoverRequest* request) {
    const auto entry = entries_.value("cover|" + request->url().toString(QUrl::FullyEncoded));
    if (entry && entry->finished && !store_->fresh(entry->cached, 30LL * 86400)) {
        entry->finished = false;
        enqueue(entry);
    }
}
void CoverCache::detach(CoverRequest* request) {
    const auto key = "cover|" + request->url().toString(QUrl::FullyEncoded);
    const auto entry = entries_.value(key);
    if (!entry) return;
    entry->watchers.removeIf([request](const auto& watcher) { return !watcher || watcher == request; });
    if (!entry->watchers.isEmpty()) return;
    entry->active = false; entries_.remove(key);
    if (entry->downloading) { --active_; entry->downloading = false; }
    if (auto* reply = entry->reply.data()) { entry->reply = nullptr; reply->abort(); reply->deleteLater(); }
    pins(); pump();
}
void CoverCache::enqueue(const std::shared_ptr<Entry>& entry) {
    pending_.removeIf([](const auto& pending) { return !pending->active; });
    const auto age = failures_.value(entry->key).secsTo(store_->now());
    if (failures_.contains(entry->key) && age >= 0 && age < 300) { entry->finished = true; setLoading(entry, false); return; }
    if (pending_.size() >= 256) { entry->finished = true; setLoading(entry, false); return; }
    setLoading(entry, true);
    pending_.enqueue(entry); pump();
}
void CoverCache::setLoading(const std::shared_ptr<Entry>& entry, bool loading) {
    for (const auto& request : std::as_const(entry->watchers)) if (request) request->setLoading(loading);
}
void CoverCache::pump() {
    while (active_ < 4 && !pending_.isEmpty()) {
        auto entry = pending_.dequeue();
        if (!entry->active) continue;
        ++active_; entry->downloading = true; download(entry);
    }
}
void CoverCache::complete(const std::shared_ptr<Entry>& entry, QUrl source, QDateTime fetchedAt) {
    if (!entry->active) return;
    if (source.isEmpty()) failures_[entry->key] = store_->now();
    else {
        failures_.remove(entry->key); entry->cached.localUrl = source; entry->cached.valid = true; entry->cached.fetchedAt = fetchedAt;
        for (const auto& request : std::as_const(entry->watchers)) if (request) request->setSource(source);
    }
    entry->finished = true;
    setLoading(entry, false);
    if (entry->downloading) { --active_; entry->downloading = false; }
    pins(); store_->inspect();
    pump();
}
void CoverCache::download(const std::shared_ptr<Entry>& entry) {
    QNetworkRequest request(entry->url);
    request.setTransferTimeout(timeoutMs_);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral(DANMAX_HTTP_USER_AGENT));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto* reply = network_.get(request); entry->reply = reply;
    reply->setReadBufferSize(maxBytes + 1);
    connect(reply, &QIODevice::readyRead, reply, [reply] { if (reply->bytesAvailable() > maxBytes) reply->abort(); });
    QTimer::singleShot(timeoutMs_, reply, [reply] { if (!reply->isFinished()) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, entry, reply] {
        const bool ok = reply->error() == QNetworkReply::NoError && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        const auto bytes = reply->isOpen() ? reply->readAll() : QByteArray{};
        entry->reply = nullptr; reply->deleteLater();
        if (!entry->active) return;
        if (!ok || bytes.size() > maxBytes) { complete(entry); return; }
        worker_.start([this, entry, bytes] {
            const auto format = imageFormat(bytes);
            QMetaObject::invokeMethod(this, [this, entry, bytes, format] {
                if (!entry->active) return;
                if (format.isEmpty()) { complete(entry); return; }
                store_->write(entry->key, true, bytes, store_->now(), [this, entry, bytes, format](MetadataCache::Record saved) {
                    if (!entry->active) return;
                    // Budget/write failures still permit the current session to display the image.
                    auto source = saved.localUrl;
                    if (source.isEmpty()) source = QUrl(QStringLiteral("data:image/") + QString::fromLatin1(format) + ";base64," + QString::fromLatin1(bytes.toBase64()));
                    complete(entry, source, saved.fetchedAt);
                });
            }, Qt::QueuedConnection);
        });
    });
}
void CoverCache::reset() {
    for (const auto& entry : std::as_const(entries_)) {
        entry->active = false;
        if (auto* reply = entry->reply.data()) { entry->reply = nullptr; reply->abort(); reply->deleteLater(); }
    }
    entries_.clear(); pending_.clear(); failures_.clear(); active_ = 0;
    requests_.removeIf([](const auto& request) { return !request; });
    for (const auto& request : std::as_const(requests_)) if (request) { request->setLoading(false); request->setSource({}); }
    pins();
}
