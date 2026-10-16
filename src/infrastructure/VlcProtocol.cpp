#include "infrastructure/VlcProtocol.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <cmath>
std::optional<VlcState> VlcProtocol::decode(const QByteArray& data, const QString& request,
                                          const QString& session, quint64 generation, double observedAt) {
    if (data.size() > 65536) return {};
    QJsonParseError parse; const auto doc = QJsonDocument::fromJson(data, &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) return {};
    const auto obj = doc.object();
    if (obj.value("protocol").toInt() != 1 || !obj.value("request").isString() || obj.value("request").toString() != request
        || !obj.value("ok").isBool() || !obj.value("found").isBool()) return {};
    VlcState result; auto& sample = result.sample;
    result.accepted = obj.value("ok").toBool(); sample.id = session; sample.generation = generation; sample.observedAt = observedAt;
    result.previousItem = obj.value("previous_item").toString();
    sample.found = obj.value("found").toBool();
    if (!sample.found) { sample.playback = MediaSample::Playback::Closed; return result; }
    for (const auto* name : {"position", "duration", "rate"}) if (!obj.value(name).isDouble()) return {};
    for (const auto* name : {"state", "uri", "item"}) if (!obj.value(name).isString()) return {};
    sample.position = obj.value("position").toDouble(); sample.duration = obj.value("duration").toDouble(); sample.rate = obj.value("rate").toDouble();
    sample.mediaUri = obj.value("uri").toString(); result.item = obj.value("item").toString();
    const QUrl uri(sample.mediaUri);
    if (!std::isfinite(sample.position) || !std::isfinite(sample.duration) || !std::isfinite(sample.rate)
        || sample.position < 0 || sample.duration <= 0 || sample.duration > 366 * 86400.0 || sample.position > sample.duration + 1
        || sample.rate <= 0 || sample.rate > 64 || !uri.isLocalFile() || !QFileInfo(uri.toLocalFile()).isAbsolute()
        || sample.mediaUri.size() > 32768 || result.item.isEmpty() || result.item.size() > 64 || sample.mediaUri.contains(QChar::Null)) return {};
    const auto state = obj.value("state").toString();
    if (state == "playing") sample.playback = MediaSample::Playback::Playing;
    else if (state == "paused") sample.playback = MediaSample::Playback::Paused;
    else if (state == "stopped") sample.playback = MediaSample::Playback::Stopped;
    else return {};
    sample.playing = sample.playback == MediaSample::Playback::Playing;
    sample.title = QFileInfo(uri.toLocalFile()).fileName();
    sample.identity = session + '\n' + sample.mediaUri + '\n' + result.item;
    sample.sessionToken = qHash(sample.identity); if (!sample.sessionToken) sample.sessionToken = 1;
    return result;
}
