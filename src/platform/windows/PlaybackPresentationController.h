#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QTimer>
#include <QScreen>
#include <QVariantMap>
#include <functional>

// GUI-only presentation; owns no media clock and never replaces the video HWND.
class PlaybackPresentationController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool watching READ watching WRITE setWatching NOTIFY changed)
    Q_PROPERTY(bool playing READ playing WRITE setPlaying NOTIFY changed)
    Q_PROPERTY(bool blocked READ blocked WRITE setBlocked NOTIFY changed)
    Q_PROPERTY(bool fullscreen READ fullscreen NOTIFY changed)
    Q_PROPERTY(bool controlsVisible READ controlsVisible NOTIFY changed)
public:
    using ForegroundQuery = std::function<bool(QWindow*)>;
    explicit PlaybackPresentationController(QObject* parent = nullptr, int idleMs = 3000, ForegroundQuery foreground = {}, std::function<QPoint()> pointer = {});
    ~PlaybackPresentationController() override;
    bool watching() const { return watching_; }
    bool playing() const { return playing_; }
    bool blocked() const { return blocked_; }
    bool fullscreen() const { return fullscreen_; }
    bool controlsVisible() const { return visible_; }
    void setWatching(bool value);
    void setPlaying(bool value);
    void setBlocked(bool value);
    Q_INVOKABLE void attach(QObject* main, QObject* controls, QObject* overlay);
    Q_INVOKABLE void toggleFullscreen(const QString& source = QStringLiteral("api"));
    Q_INVOKABLE void exitFullscreen();
    Q_INVOKABLE void interact(bool keyboard = false);
    void sample();
    bool handleKey(int key);
    QVariantMap diagnostics() const;
signals:
    void changed();
    void aboutToChangeHost();
    void keyboardControlsRequested();
    void pointerInteraction();
    void keyboardInteraction();
protected:
    bool eventFilter(QObject*, QEvent*) override;
private:
    bool owns(QObject*) const;
    bool popupOpen() const;
    void publish(bool visible);
    void cursor(bool hidden);
    QPointer<QQuickWindow> main_, controls_;
    QPointer<QWindow> overlay_;
    QList<QPointer<QObject>> popups_;
    QPointer<QScreen> restoreScreen_;
    QRect restoreGeometry_, normalGeometry_;
    Qt::WindowState restoreState_{Qt::WindowNoState};
    QElapsedTimer idle_;
    QTimer timer_;
    QPoint pointer_;
    ForegroundQuery foreground_;
    std::function<QPoint()> pointerQuery_;
    int idleMs_;
    bool watching_{}, playing_{}, blocked_{}, fullscreen_{}, visible_{true}, foregroundLast_{}, blankCursor_{};
};
