#pragma once
#include "infrastructure/MediaPlaybackBackend.h"
#include <QByteArray>
#include <optional>
struct VlcState { MediaSample sample; QString item, previousItem; bool accepted{}; };
namespace VlcProtocol {
std::optional<VlcState> decode(const QByteArray& data, const QString& request,
                              const QString& session, quint64 generation, double observedAt);
}
