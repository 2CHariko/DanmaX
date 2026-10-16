#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>

struct MediaSample {
    enum class Playback { Unknown, Playing, Paused, Stopped, Closed, Changing };
    QString id, title, identity, mediaUri;
    double position{}, duration{}, rate{1};
    bool playing{}, found{}, canSeek{};
    Playback playback{Playback::Unknown};
    quint64 generation{}, sessionToken{};
    double observedAt{};
};
Q_DECLARE_METATYPE(MediaSample)

struct ManagedPlaybackOpen { QString sessionId, error; };

// Infrastructure port. Backends own their connection and control semantics;
// consumers receive the same bounded, generation-tagged media snapshots.
class MediaPlaybackBackend : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual bool accepts(const QString& id) const = 0;
    virtual void select(const QString& id, quint64 generation) = 0;
    virtual void requestPlayback(MediaSample expected, bool play, quint64 request) = 0;
    virtual void requestSeek(MediaSample expected, double seconds, quint64 request) = 0;
  signals:
    void sessionsChanged(QVariantList sessions);
    void sampleReceived(MediaSample sample);
    void errorOccurred(QString error);
    void playbackControlFinished(quint64 request, bool success, QString error);
    void seekFinished(quint64 request, bool success, QString error);
};
