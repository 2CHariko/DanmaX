#include "platform/windows/VideoViewportController.h"
#include <QBackingStore>
#include <QPainter>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTimer>
#include <QExposeEvent>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <QPlatformSurfaceEvent>
#include <QScopedValueRollback>
#include <functional>
#include <algorithm>
#include <windows.h>
#include <commctrl.h>

// GUI-thread adapter for our own windows only. The SDK subclass preserves both
// Qt's and QWindowKit's procedure chain, including the native move/resize loop.
class NativeOverlayFollower final {
public:
    explicit NativeOverlayFollower(VideoViewportController* owner, std::function<bool()> eligible)
        : owner_(owner), eligible_(std::move(eligible)) {}
    ~NativeOverlayFollower() { detach(); }
    void detach() {
        ++revision_;
        if (hostHandle_) RemoveWindowSubclass(hostHandle_, procedure, id());
        if (videoHandle_) RemoveWindowSubclass(videoHandle_, procedure, id());
        hostHandle_ = videoHandle_ = overlayHandle_ = nullptr;
        host_.clear(); video_.clear(); overlay_.clear();
    }
    bool bind(QWindow* host, QWindow* video, QWindow* overlay, bool enabled) {
        const auto h = existingHandle(host), v = existingHandle(video), o = existingHandle(overlay);
        if (!enabled || !h || !v || !o || video->parent() != host || overlay->transientParent() != host) {
            if (hostHandle_ || videoHandle_) detach();
            return false;
        }
        if (h != hostHandle_ || v != videoHandle_ || o != overlayHandle_) {
            detach(); host_ = host; video_ = video; overlay_ = overlay;
            hostHandle_ = h; videoHandle_ = v; overlayHandle_ = o;
            if (!SetWindowSubclass(h, procedure, id(), reinterpret_cast<DWORD_PTR>(this)) ||
                !SetWindowSubclass(v, procedure, id(), reinterpret_cast<DWORD_PTR>(this))) {
                ++failures_; detach(); return false;
            }
        }
        synchronize();
        return true;
    }
    QVariantMap metrics() const {
        return {{"strategy", "native-video-window"}, {"hooked", bool(hostHandle_ && videoHandle_)},
            {"nativeEvents", qulonglong(events_)}, {"corrections", qulonglong(corrections_)},
            {"nativeCorrections", qulonglong(nativeCorrections_)}, {"failures", qulonglong(failures_)},
            {"moveLoopEntries", qulonglong(moveEntries_)}, {"moveLoopExits", qulonglong(moveExits_)},
            {"maxBeforeEventPx", maxBefore_}, {"maxAfterEventPx", maxAfter_}, {"currentDifferencePx", difference()}};
    }
private:
    static HWND existingHandle(QWindow* window) {
        // winId() creates a surface. Never call it for an absent/dying surface.
        return window && window->handle() ? reinterpret_cast<HWND>(window->winId()) : nullptr;
    }
    UINT_PTR id() const { return reinterpret_cast<UINT_PTR>(this); }
    int difference() const {
        RECT a{}, b{};
        if (!video_ || !overlay_ || !videoHandle_ || !overlayHandle_ || !GetWindowRect(videoHandle_, &a) || !GetWindowRect(overlayHandle_, &b)) return -1;
        return int(std::max({std::abs(a.left - b.left), std::abs(a.top - b.top),
            std::abs(a.right - b.right), std::abs(a.bottom - b.bottom)}));
    }
    bool synchronize() {
        if (syncing_ || !host_ || !video_ || !overlay_ || !eligible_()
            || overlay_->transientParent() != host_ || video_->parent() != host_
            || !IsWindowVisible(hostHandle_) || !IsWindowVisible(videoHandle_) || IsIconic(hostHandle_)) return false;
        QScopedValueRollback guard(syncing_, true);
        RECT target{}, current{};
        if (!GetWindowRect(videoHandle_, &target) || !GetWindowRect(overlayHandle_, &current)) { ++failures_; return false; }
        // A bounded native correction can round differently from Qt's logical
        // position. Do not write the logical rectangle back every frame when
        // the real surfaces already align: that would create a correction loop.
        if (EqualRect(&target, &current)) return true;
        // Maintain Qt's scene size before any native correction.
        const QRect logical(video_->mapToGlobal(QPoint()), video_->size());
        if (overlay_->geometry() != logical) overlay_->setGeometry(logical);
        if (!GetWindowRect(overlayHandle_, &current)) { ++failures_; return false; }
        if (EqualRect(&target, &current)) return true;
        UINT flags = SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER;
        if (target.right - target.left == current.right - current.left &&
            target.bottom - target.top == current.bottom - current.top) flags |= SWP_NOSIZE;
        ++nativeCorrections_;
        if (!SetWindowPos(overlayHandle_, nullptr, target.left, target.top,
            target.right - target.left, target.bottom - target.top, flags) || difference() != 0) {
            ++failures_; return false;
        }
        return true;
    }
    static LRESULT CALLBACK procedure(HWND window, UINT message, WPARAM w, LPARAM l, UINT_PTR id, DWORD_PTR data) {
        auto* self = reinterpret_cast<NativeOverlayFollower*>(data);
        if (message == WM_NCDESTROY) {
            RemoveWindowSubclass(window, procedure, id);
            if (window == self->hostHandle_) self->hostHandle_ = nullptr;
            if (window == self->videoHandle_) self->videoHandle_ = nullptr;
            ++self->revision_;
            return DefSubclassProc(window, message, w, l);
        }
        const QPointer<VideoViewportController> alive(self->owner_);
        const auto revision = self->revision_;
        if (message == WM_ENTERSIZEMOVE) ++self->moveEntries_;
        if (message == WM_EXITSIZEMOVE) ++self->moveExits_;
        const auto result = DefSubclassProc(window, message, w, l);
        if (alive && revision == self->revision_ && (message == WM_WINDOWPOSCHANGED || message == WM_EXITSIZEMOVE)
            && !self->syncing_) {
            const int before = self->difference();
            self->maxBefore_ = std::max(self->maxBefore_, before);
            if (self->synchronize()) {
                ++self->events_;
                if (before > 0) ++self->corrections_;
                self->maxAfter_ = std::max(self->maxAfter_, self->difference());
            }
        }
        return result;
    }
    VideoViewportController* owner_;
    std::function<bool()> eligible_;
    QPointer<QWindow> host_, video_, overlay_;
    HWND hostHandle_{}, videoHandle_{}, overlayHandle_{};
    bool syncing_{};
    quint64 revision_{}, events_{}, corrections_{}, nativeCorrections_{}, failures_{}, moveEntries_{}, moveExits_{};
    int maxBefore_{}, maxAfter_{};
};

namespace {
class VideoWindow final : public QWindow {
public:
    std::function<void(bool, QString, bool)> drag;
private:
    bool event(QEvent* event) override {
        if (event->type() == QEvent::DragLeave) { if (drag) drag(false, {}, false); return true; }
        if (event->type() == QEvent::DragEnter || event->type() == QEvent::DragMove || event->type() == QEvent::Drop) {
            auto* drop = static_cast<QDropEvent*>(event);
            const auto urls = drop->mimeData()->urls();
            const auto path = urls.size() == 1 && urls.first().isLocalFile() ? urls.first().toLocalFile() : QString();
            const QFileInfo file(path);
            const QStringList extensions{"mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "m4v", "ts", "m2ts"};
            const bool valid = file.isFile() && extensions.contains(file.suffix().toLower()) && (drop->possibleActions() & (Qt::CopyAction | Qt::LinkAction));
            if (valid) { drop->setDropAction(drop->possibleActions() & Qt::CopyAction ? Qt::CopyAction : Qt::LinkAction); drop->accept(); }
            else drop->ignore();
            if (drag) drag(valid && event->type() != QEvent::Drop, valid ? path : QString(), valid && event->type() == QEvent::Drop);
            return true;
        }
        return QWindow::event(event);
    }
    QBackingStore backing_{this};
    void paint() {
        if (!isExposed() || size().isEmpty()) return;
        backing_.resize(size()); const QRect region(QPoint(), size()); backing_.beginPaint(region);
        { QPainter painter(backing_.paintDevice()); painter.fillRect(region, Qt::black); }
        backing_.endPaint(); backing_.flush(region);
    }
    void exposeEvent(QExposeEvent*) override { paint(); }
    void resizeEvent(QResizeEvent*) override { paint(); }
};
}
VideoViewportController::VideoViewportController(QObject* parent) : QObject(parent), video_(std::make_unique<VideoWindow>()) {
    follower_ = std::make_unique<NativeOverlayFollower>(this, [this] {
        return active_ && item_ && item_->isVisible() && host_ && host_->isVisible()
            && host_->windowState() != Qt::WindowMinimized && item_->width() >= 1 && item_->height() >= 1;
    });
    static_cast<VideoWindow*>(video_.get())->drag = [this](bool dragging, const QString& path, bool dropped) {
        dragging_ = dragging; dragName_ = QFileInfo(path).fileName(); emit changed();
        if (dropped && active_ && shown_) emit videoDropped(path);
    };
    auto format = video_->format(); format.setAlphaBufferSize(0); video_->setFormat(format);
    // A GUI event filter covers layout changes, ownership, DPI and minimization.
    qGuiApp->installEventFilter(this);
}
VideoViewportController::~VideoViewportController() { follower_->detach(); qGuiApp->removeEventFilter(this); }
quintptr VideoViewportController::nativeHandle() { return video_->winId(); }
void VideoViewportController::attach(QQuickItem* item) {
    if (item_ == item) return;
    if (item_) disconnect(item_, nullptr, this, nullptr);
    item_ = item;
    if (item_) {
        for (auto signal : {&QQuickItem::xChanged, &QQuickItem::yChanged, &QQuickItem::widthChanged, &QQuickItem::heightChanged, &QQuickItem::visibleChanged})
            connect(item_, signal, this, &VideoViewportController::scheduleSynchronize);
        // Stop native ownership immediately on page hiding; layout updates are
        // coalesced, but a desktop task must never be reclaimed by a late move.
        connect(item_, &QQuickItem::visibleChanged, this, [this] { if (!item_->isVisible()) follower_->detach(); });
        connect(item_, &QQuickItem::windowChanged, this, [this] { synchronize(); });
        connect(item_, &QObject::destroyed, this, [this] { item_ = nullptr; synchronize(); });
    }
    synchronize();
}
void VideoViewportController::setOverlay(QWindow* overlay) {
    if (overlay_ != overlay) follower_->detach();
    overlay_ = overlay; synchronize();
}
void VideoViewportController::setActive(bool active) {
    if (active_ == active) return;
    active_ = active; if (active) releaseStatus_.clear(); synchronize(); emit changed();
}
QVariantMap VideoViewportController::followMetrics() const { return follower_->metrics(); }
void VideoViewportController::scheduleSynchronize() {
    if (synchronizePending_) return;
    synchronizePending_ = true;
    QTimer::singleShot(0, this, [this] { synchronizePending_ = false; synchronize(); });
}
void VideoViewportController::synchronize() {
    if (synchronizing_) return;
    QScopedValueRollback guard(synchronizing_, true);
    auto* host = item_ ? item_->window() : nullptr;
    if (host_ != host) follower_->detach();
    host_ = host;
    const bool shown = active_ && item_ && item_->isVisible() && host_ && host_->isVisible()
        && host_->windowState() != Qt::WindowMinimized && item_->width() >= 1 && item_->height() >= 1;
    if (!shown) follower_->detach();
    // WindowContainer remains the sole owner of video parenting and size.
    // Never recreate a surface while the media worker may be using its HWND.
    video_->setVisible(shown);
    if (overlay_ && active_ && host_ && item_->isVisible()) {
        if (overlay_->transientParent() != host_) overlay_->setTransientParent(host_);
        if (overlay_->flags().testFlag(Qt::WindowStaysOnTopHint)) overlay_->setFlag(Qt::WindowStaysOnTopHint, false);
    }
    const bool nativeFollow = follower_->bind(host_, video_.get(), overlay_, shown);
    if (!nativeFollow && overlay_ && active_ && host_ && item_->isVisible()) {
        const bool nativeVideo = video_->handle() && video_->parent() == host_;
        const QRect region = nativeVideo ? QRect(video_->mapToGlobal(QPoint()), video_->size())
            : QRect(item_->mapToGlobal(QPointF()).toPoint(), QSize(qRound(item_->width()), qRound(item_->height())));
        if (overlay_->geometry() != region) overlay_->setGeometry(region);
    }
    if (shown_ != shown) { shown_ = shown; emit changed(); }
}
void VideoViewportController::placeDialog(QQuickWindow* dialog) {
    const QPointer<QWindow> owner = host_;
    QTimer::singleShot(0, dialog, [dialog, owner] {
        if (!owner || !dialog->isVisible()) return;
        QPoint point = owner->position() + QPoint((owner->width() - dialog->width()) / 2, (owner->height() - dialog->height()) / 2);
        if (owner->screen()) {
            const auto area = owner->screen()->availableGeometry().adjusted(8, 8, -8, -8);
            point.setX(std::clamp(point.x(), area.left(), std::max(area.left(), area.right() - dialog->width() + 1)));
            point.setY(std::clamp(point.y(), area.top(), std::max(area.top(), area.bottom() - dialog->height() + 1)));
        }
        if (dialog->position() != point) dialog->setPosition(point);
    });
}
bool VideoViewportController::eventFilter(QObject* object, QEvent* event) {
    if (object == host_ || object == video_.get() || object == overlay_) {
        if (event->type() == QEvent::PlatformSurface) {
            auto* surface = static_cast<QPlatformSurfaceEvent*>(event);
            if (surface->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) follower_->detach();
            else scheduleSynchronize();
        }
        if (event->type() == QEvent::WinIdChange) { follower_->detach(); scheduleSynchronize(); }
    }
    if (active_ && object == video_.get()) {
        if (event->type() == QEvent::KeyPress) emit keyPressed(static_cast<QKeyEvent*>(event)->key());
        if (event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonPress || event->type() == QEvent::TouchBegin) emit interaction();
    }
    if (active_ && event->type() == QEvent::Show) {
        if (auto* quick = qobject_cast<QQuickWindow*>(object); quick && quick->type() == Qt::Dialog
            && quick != host_ && quick->transientParent() == host_) {
            // A native framed popup cannot reliably expose alpha through DWM.
            // Keep the official Dialog and its native frame; give the window's
            // shadow margins the actual owner theme instead of the white default.
            auto* owner = qobject_cast<QQuickWindow*>(host_.data());
            const QColor color = owner ? owner->color() : Qt::black;
            if (owner) connect(owner, &QQuickWindow::colorChanged, quick, &QQuickWindow::setColor, Qt::UniqueConnection);
            quick->setColor(color);
            QTimer::singleShot(0, quick, [quick, color] { quick->setColor(color); });
            placeDialog(quick);
        }
    }
    if (active_ && event->type() == QEvent::Resize) {
        if (auto* quick = qobject_cast<QQuickWindow*>(object); quick && quick->type() == Qt::Dialog && quick->transientParent() == host_)
            placeDialog(quick);
    }
    if ((object == host_ || object == video_.get()) && (event->type() == QEvent::Move || event->type() == QEvent::Resize
        || event->type() == QEvent::Show || event->type() == QEvent::Hide || event->type() == QEvent::WindowStateChange
        || event->type() == QEvent::ScreenChangeInternal || event->type() == QEvent::DevicePixelRatioChange)) scheduleSynchronize();
    return false;
}
