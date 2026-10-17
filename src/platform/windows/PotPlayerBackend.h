#pragma once
#include "infrastructure/MediaPlaybackBackend.h"
#include <memory>

// Only instances launched here are owned. Existing PotPlayer processes remain
// on the explicitly selected SMTC path and are never closed by this backend.
class PotPlayerBackend final : public MediaPlaybackBackend {
    Q_OBJECT
public:
    explicit PotPlayerBackend(QObject* parent = nullptr);
    ~PotPlayerBackend() override;
    bool accepts(const QString& id) const override { return id.startsWith("danmax-potplayer:"); }
    ManagedPlaybackOpen open(const QString& executable, const QString& video);
    void closeOwned(const QString& id = {});
    void select(const QString& id, quint64 generation) override;
    void requestPlayback(MediaSample expected, bool play, quint64 request) override;
    void requestSeek(MediaSample expected, double seconds, quint64 request) override;
signals:
    void ownedExited(QString session);
    void closeFailed(QString error);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
