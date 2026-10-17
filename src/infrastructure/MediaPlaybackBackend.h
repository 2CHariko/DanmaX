#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QUrl>
#include <cmath>

struct MediaSample {
    enum class Playback { Unknown, Playing, Paused, Stopped, Closed, Changing };
    enum class Connection { Unknown, Alive, Unavailable, Exited };
    enum class Evidence { Unknown, LaunchTarget, Verified };
    enum class SeekPrecision { Unspecified, Precise };
    QString id, title, identity, mediaUri;
    QString launchUri, contentId;
    double position{}, duration{}, rate{1};
    bool playing{}, found{}, canSeek{}, canPlay{}, canPause{};
    Playback playback{Playback::Unknown};
    Connection connection{Connection::Unknown};
    Evidence evidence{Evidence::Unknown};
    SeekPrecision seekPrecision{SeekPrecision::Unspecified};
    // sessionToken identifies the connection, never a title or playlist item.
    quint64 generation{}, sessionToken{};
    double observedAt{};
    bool sameConnection(const MediaSample& other) const {
        return id == other.id && generation == other.generation && sessionToken == other.sessionToken;
    }
    bool contentDiffers(const MediaSample& other) const {
        // Unknown evidence cannot prove a change. A launch target is an intent,
        // not an independently observed path; a verified path can contradict it.
        if (evidence == Evidence::Verified && other.evidence == Evidence::Verified &&
            !contentId.isEmpty() && !other.contentId.isEmpty() && contentId != other.contentId) return true;
        const auto a = evidence == Evidence::Verified ? mediaUri : evidence == Evidence::LaunchTarget ? launchUri : QString();
        const auto b = other.evidence == Evidence::Verified ? other.mediaUri : other.evidence == Evidence::LaunchTarget ? other.launchUri : QString();
        return (evidence == Evidence::Verified || other.evidence == Evidence::Verified) &&
            !a.isEmpty() && !b.isEmpty() && QUrl(a) != QUrl(b);
    }
    bool usableTimeline() const {
        return found && connection != Connection::Exited && connection != Connection::Unavailable &&
            std::isfinite(position) && position >= 0 && std::isfinite(duration) && duration > 0 &&
            duration <= 366 * 86400.0 && position <= duration + 1 &&
            (playback == Playback::Playing || playback == Playback::Paused);
    }
};
Q_DECLARE_METATYPE(MediaSample)

struct SeekDispatch {
    enum class Outcome { Rejected, Submitted, Unknown };
    quint64 request{};
    Outcome outcome{Outcome::Rejected};
    MediaSample before;
    double issuedAt{}, returnedAt{};
    QString error;
};
Q_DECLARE_METATYPE(SeekDispatch)

struct ManagedPlaybackOpen { QString sessionId, error; };

// Infrastructure port. Backends own their connection and control semantics;
// consumers receive the same bounded, generation-tagged media snapshots.
class MediaPlaybackBackend : public QObject {
    Q_OBJECT
  public:
    enum class Presentation { DesktopOverlay, EmbeddedViewport };
    using QObject::QObject;
    virtual Presentation presentation() const { return Presentation::DesktopOverlay; }
    virtual QString adapterId() const = 0;
    virtual bool accepts(const QString& id) const = 0;
    virtual ManagedPlaybackOpen open(const QString& executable, const QString& video) = 0;
    virtual void cancelOpen() = 0;
    virtual bool closesWithTask() const { return false; }
    virtual void closeOwned(const QString&) {}
    virtual void select(const QString& id, quint64 generation) = 0;
    virtual void requestPlayback(MediaSample expected, bool play, quint64 request) = 0;
    virtual void requestSeek(MediaSample expected, double seconds, quint64 request) = 0;
  signals:
    void sessionsChanged(QVariantList sessions);
    void sampleReceived(MediaSample sample);
    void errorOccurred(QString error);
    void playbackControlFinished(quint64 request, bool success, QString error);
    void seekFinished(quint64 request, bool success, QString error);
    void seekDispatched(SeekDispatch result);
  protected:
    // seekFinished is the raw transport acknowledgement, not a completed seek.
    // Product transactions consume the detailed disposition and position reads.
    void reportSeek(quint64 request, SeekDispatch::Outcome outcome, const QString& error = {},
                    const MediaSample& before = {}, double issuedAt = 0, double returnedAt = 0) {
        emit seekDispatched({request, outcome, before, issuedAt, returnedAt, error});
        emit seekFinished(request, outcome == SeekDispatch::Outcome::Submitted, error);
    }
};
