#pragma once
#include "infrastructure/MediaPlaybackBackend.h"
#include <QThread>
#include <QSet>
#include <QVariantMap>
#include <atomic>
#include <memory>
class LibVlcWorker;

// Owns the C API on one worker. HWND ownership remains with the GUI viewport.
class LibVlcBackend final : public MediaPlaybackBackend {
    Q_OBJECT
public:
    explicit LibVlcBackend(QObject* parent = nullptr);
    ~LibVlcBackend() override;
    QString adapterId() const override { return "libvlc"; }
    bool accepts(const QString& id) const override { return id.startsWith("danmax-libvlc:"); }
    Presentation presentation() const override { return Presentation::EmbeddedViewport; }
    ManagedPlaybackOpen open(const QString&, const QString& video) override;
    void cancelOpen() override;
    bool closesWithTask() const override { return true; }
    void closeOwned(const QString& id) override;
    void select(const QString& id, quint64 generation) override;
    void requestPlayback(MediaSample expected, bool play, quint64 request) override;
    void requestSeek(MediaSample expected, double seconds, quint64 request) override;
    void setVideoOutput(quintptr handle) { handle_ = handle; }
    bool busy() const { return !resources_.isEmpty(); }
    QVariantMap metrics() const { return metrics_; }
    static QString runtimeDirectory();
    static QString preflight();
signals:
    void activityChanged();
    void released(QString id);
    void closeDelayed(QString message);
private:
    QThread thread_;
    LibVlcWorker* worker_{};
    QSet<QString> resources_, closing_;
    QString current_;
    quintptr handle_{};
    QVariantMap metrics_;
    std::shared_ptr<std::atomic<quint64>> intent_{std::make_shared<std::atomic<quint64>>(0)};
};
