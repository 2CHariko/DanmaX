#include "platform/windows/PlaybackPresentationController.h"
#include "platform/windows/MediaMonitor.h"
#include <QGuiApplication>
#include <QQuickItem>
#include <QCursor>
#include <QEvent>
#include <QKeyEvent>
#include <QDebug>
#include <QSet>
#include <algorithm>
#include <windows.h>

PlaybackPresentationController::PlaybackPresentationController(QObject* parent, int idleMs, ForegroundQuery foreground, std::function<QPoint()> pointer)
    : QObject(parent), foreground_(foreground ? std::move(foreground) : ForegroundQuery([](QWindow* w) {
        return w && w->handle() && MediaMonitor::windowOrOwnerForeground(w->winId());
    })), pointerQuery_(pointer ? std::move(pointer) : std::function<QPoint()>([] { return QCursor::pos(); })), idleMs_(idleMs) {
    qGuiApp->installEventFilter(this);
    timer_.setInterval(50);
    connect(&timer_, &QTimer::timeout, this, &PlaybackPresentationController::sample);
    idle_.start();
}
PlaybackPresentationController::~PlaybackPresentationController() { cursor(false); qGuiApp->removeEventFilter(this); }
void PlaybackPresentationController::attach(QObject* main, QObject* controls, QObject* overlay) {
    main_ = qobject_cast<QQuickWindow*>(main); controls_ = qobject_cast<QQuickWindow*>(controls); overlay_ = qobject_cast<QWindow*>(overlay);
    popups_.clear();
    if (main_) {
        normalGeometry_ = main_->geometry();
        for (auto* object : main_->findChildren<QObject*>()) {
            if (!qobject_cast<QWindow*>(object) && object->metaObject()->indexOfProperty("visible") >= 0
                && (object->metaObject()->indexOfProperty("popupType") >= 0 || object->metaObject()->indexOfProperty("modality") >= 0)) popups_.append(object);
        }
    }
    if (controls_ && !controls_->handle()) { auto format = controls_->format(); format.setAlphaBufferSize(8); controls_->setFormat(format); }
}
void PlaybackPresentationController::setWatching(bool value) {
    if (watching_ == value) return;
    watching_ = value; if (!value) exitFullscreen(); emit changed();
}
void PlaybackPresentationController::setPlaying(bool value) { if (playing_ != value) { playing_ = value; interact(); emit changed(); } }
void PlaybackPresentationController::setBlocked(bool value) { if (blocked_ != value) { blocked_ = value; interact(); emit changed(); } }
void PlaybackPresentationController::toggleFullscreen(const QString& source) {
    qInfo() << "Playback presentation: toggle from" << source << "fullscreen" << fullscreen_;
    if (fullscreen_) { exitFullscreen(); return; }
    if (!watching_ || !main_) return;
    emit aboutToChangeHost();
    restoreScreen_ = main_->screen(); restoreState_ = main_->windowState();
    restoreGeometry_ = restoreState_ == Qt::WindowNoState ? main_->geometry() : normalGeometry_;
    qInfo() << "Playback presentation: enter fullscreen";
    fullscreen_ = true; pointer_ = pointerQuery_(); foregroundLast_ = false; idle_.restart();
    emit changed(); main_->showFullScreen(); timer_.start(); sample();
}
void PlaybackPresentationController::exitFullscreen() {
    if (!fullscreen_) return;
    qInfo() << "Playback presentation: exit fullscreen; watching" << watching_;
    emit aboutToChangeHost(); fullscreen_ = false; timer_.stop(); cursor(false);
    if (controls_) controls_->hide();
    if (main_) {
        auto* screen = restoreScreen_ ? restoreScreen_.data() : main_->screen();
        if (!screen) screen = QGuiApplication::primaryScreen();
        if (screen) main_->setScreen(screen);
        auto geometry = restoreGeometry_;
        if (!restoreScreen_ && screen && geometry.isValid()) {
            const auto area = screen->availableGeometry();
            geometry.setSize(geometry.size().boundedTo(area.size()));
            geometry.moveLeft(std::clamp(geometry.left(), area.left(), area.right() - geometry.width() + 1));
            geometry.moveTop(std::clamp(geometry.top(), area.top(), area.bottom() - geometry.height() + 1));
        }
        main_->showNormal();
        if (geometry.isValid()) main_->setGeometry(geometry);
        if (restoreState_ == Qt::WindowMaximized) main_->showMaximized();
    }
    visible_ = true; emit changed();
}
void PlaybackPresentationController::interact(bool keyboard) {
    idle_.restart();
    if (!fullscreen_) return;
    cursor(false); sample();
    if (keyboard && controls_ && visible_) {
        controls_->requestActivate();
        // Keyboard focus is requested only after an explicit keyboard gesture.
    }
}
bool PlaybackPresentationController::owns(QObject* object) const {
    auto* window = qobject_cast<QWindow*>(object);
    if (auto* item = qobject_cast<QQuickItem*>(object)) window = item->window();
    return window && (window == main_ || window == controls_);
}
bool PlaybackPresentationController::popupOpen() const {
    for (const auto& popup : popups_) if (popup && popup->property("visible").toBool()) return true;
    if (QGuiApplication::modalWindow()) return true;
    for (auto* window : QGuiApplication::allWindows()) {
        if (!window->isVisible() || (window->type() != Qt::Popup && window->type() != Qt::Dialog)) continue;
        for (auto* parent = window->transientParent(); parent; parent = parent->transientParent())
            if (parent == main_ || parent == controls_) return true;
    }
    if (main_ && main_->handle()) {
        const auto fg = GetAncestor(GetForegroundWindow(), GA_ROOT);
        if (MediaMonitor::windowOrOwnerForeground(main_->winId()) && fg != reinterpret_cast<HWND>(main_->winId())
            && (!controls_ || fg != reinterpret_cast<HWND>(controls_->winId()))) return true;
    }
    return false;
}
void PlaybackPresentationController::cursor(bool hidden) {
    if (hidden == blankCursor_) return;
    blankCursor_ = hidden;
    if (hidden) QGuiApplication::setOverrideCursor(Qt::BlankCursor); else QGuiApplication::restoreOverrideCursor();
}
void PlaybackPresentationController::publish(bool show) {
    if (controls_ && controls_->isVisible() != show) {
        if (!show && controls_->isActive() && main_ && foreground_(main_)) main_->requestActivate();
        controls_->setVisible(show);
        if (show && controls_->handle()) {
            const auto control = reinterpret_cast<HWND>(controls_->winId());
            ShowWindow(control, SW_SHOWNOACTIVATE);
            HWND after = HWND_TOP;
            if (popupOpen()) {
                QSet<quintptr> popupHandles;
                const auto foreground = GetAncestor(GetForegroundWindow(), GA_ROOT);
                if (foreground && foreground != control && foreground != reinterpret_cast<HWND>(main_->winId())) popupHandles.insert(quintptr(foreground));
                for (auto* window : QGuiApplication::allWindows()) {
                    if (!window->isVisible() || (window->type() != Qt::Popup && window->type() != Qt::Dialog)) continue;
                    for (auto* owner = window->transientParent(); owner; owner = owner->transientParent())
                        if (owner == main_ || owner == controls_) { popupHandles.insert(window->winId()); break; }
                }
                // Insert below the lowest owned popup, not merely the active menu.
                for (auto window = GetTopWindow(nullptr); window; window = GetWindow(window, GW_HWNDNEXT))
                    if (popupHandles.contains(quintptr(window))) after = window;
            }
            SetWindowPos(control, after, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }
    if (visible_ != show) { visible_ = show; emit changed(); }
}
void PlaybackPresentationController::sample() {
    if (!fullscreen_ || !main_ || !controls_) return;
    const bool foreground = main_->isVisible() && main_->windowState() != Qt::WindowMinimized && foreground_(main_);
    if (!foreground) { foregroundLast_ = false; cursor(false); publish(false); return; }
    const QPoint point = pointerQuery_();
    if (!foregroundLast_ || (point - pointer_).manhattanLength() >= 2) { pointer_ = point; idle_.restart(); }
    foregroundLast_ = true;
    const auto global = main_->mapToGlobal(QPoint(0, main_->height() - controls_->height()));
    const QRect geometry(global, QSize(main_->width(), controls_->height()));
    if (controls_->geometry() != geometry) controls_->setGeometry(geometry);
    const bool popup = popupOpen();
    const bool hover = controls_->isVisible() && geometry.contains(point);
    const bool show = !playing_ || blocked_ || popup || hover || idle_.elapsed() < idleMs_;
    publish(show); cursor(false);
    // Repair relative order only when needed, never above a business popup.
    if (show && !popup && overlay_ && overlay_->isVisible()) {
        const auto control = reinterpret_cast<HWND>(controls_->winId());
        bool above = false;
        for (auto w = GetWindow(reinterpret_cast<HWND>(overlay_->winId()), GW_HWNDPREV); w; w = GetWindow(w, GW_HWNDPREV))
            if (w == control) { above = true; break; }
        if (!above) SetWindowPos(control, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}
QVariantMap PlaybackPresentationController::diagnostics() const {
    return {{"watching", watching_}, {"fullscreen", fullscreen_}, {"controlsVisible", visible_},
        {"controlsActive", controls_ && controls_->isActive()}, {"blocked", blocked_}, {"popupOpen", popupOpen()},
        {"controlFocus", controls_ && controls_->activeFocusItem() ? controls_->activeFocusItem()->objectName() : QString()},
        {"mainState", main_ ? int(main_->windowState()) : -1}, {"idleMs", idle_.elapsed()}};
}
bool PlaybackPresentationController::handleKey(int key) {
    if (!watching_ || popupOpen()) return false;
    if (key == Qt::Key_F11) { qInfo() << "Playback presentation: F11"; toggleFullscreen(QStringLiteral("F11")); return true; }
    if (!fullscreen_) return false;
    interact();
    if (key == Qt::Key_Escape) { qInfo() << "Playback presentation: Escape"; exitFullscreen(); return true; }
    if (key == Qt::Key_Tab) { emit keyboardControlsRequested(); return true; }
    return false;
}
bool PlaybackPresentationController::eventFilter(QObject* object, QEvent* event) {
    if (object == main_ && !fullscreen_ && main_->windowState() == Qt::WindowNoState
        && (event->type() == QEvent::Move || event->type() == QEvent::Resize)) normalGeometry_ = main_->geometry();
    if (!owns(object)) return false;
    if (event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (fullscreen_ && (object == controls_ || (qobject_cast<QQuickItem*>(object) && qobject_cast<QQuickItem*>(object)->window() == controls_))) emit keyboardInteraction();
        if (!key->isAutoRepeat() && (key->key() != Qt::Key_Tab || object == main_) && handleKey(key->key())) return true;
    }
    if (!fullscreen_) return false;
    if (event->type() == QEvent::MouseButtonPress) emit pointerInteraction();
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::TouchBegin || event->type() == QEvent::MouseButtonPress
        || event->type() == QEvent::Wheel) { idle_.restart(); cursor(false); }
    return false;
}
