#include "platform/windows/VideoViewportController.h"
#include <QBackingStore>
#include <QPainter>
#include <QGuiApplication>
#include <QQuickWindow>
#include <QTimer>
#include <QExposeEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QFileInfo>
#include <functional>
#include <algorithm>

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
    static_cast<VideoWindow*>(video_.get())->drag = [this](bool dragging, const QString& path, bool dropped) {
        dragging_ = dragging; dragName_ = QFileInfo(path).fileName(); emit changed();
        if (dropped && active_ && shown_) emit videoDropped(path);
    };
    auto format = video_->format(); format.setAlphaBufferSize(0); video_->setFormat(format);
    // A GUI event filter covers layout changes, ownership, DPI and minimization.
    qGuiApp->installEventFilter(this);
}
VideoViewportController::~VideoViewportController() { qGuiApp->removeEventFilter(this); }
quintptr VideoViewportController::nativeHandle() { return video_->winId(); }
void VideoViewportController::attach(QQuickItem* item) {
    if (item_ == item) return;
    if (item_) disconnect(item_, nullptr, this, nullptr);
    item_ = item;
    if (item_) {
        for (auto signal : {&QQuickItem::xChanged, &QQuickItem::yChanged, &QQuickItem::widthChanged, &QQuickItem::heightChanged, &QQuickItem::visibleChanged})
            connect(item_, signal, this, &VideoViewportController::synchronize, Qt::QueuedConnection);
        connect(item_, &QQuickItem::windowChanged, this, [this] { synchronize(); });
        connect(item_, &QObject::destroyed, this, [this] { item_ = nullptr; synchronize(); });
    }
    synchronize();
}
void VideoViewportController::setOverlay(QWindow* overlay) { overlay_ = overlay; synchronize(); }
void VideoViewportController::setActive(bool active) {
    if (active_ == active) return;
    active_ = active; if (active) releaseStatus_.clear(); synchronize(); emit changed();
}
void VideoViewportController::synchronize() {
    host_ = item_ ? item_->window() : nullptr;
    const bool shown = active_ && item_ && item_->isVisible() && host_ && host_->isVisible()
        && host_->windowState() != Qt::WindowMinimized && item_->width() >= 1 && item_->height() >= 1;
    if (overlay_ && active_ && host_ && item_->isVisible()) {
        overlay_->setTransientParent(host_);
        overlay_->setFlag(Qt::WindowStaysOnTopHint, false);
        const auto point = item_->mapToGlobal(QPointF());
        const QRect region(point.toPoint(), QSize(qRound(item_->width()), qRound(item_->height())));
        if (overlay_->geometry() != region) overlay_->setGeometry(region);
    }
    // WindowContainer owns geometry and native parenting. Do not replace its
    // parent or destroy the surface while the worker may still use the HWND.
    video_->setVisible(shown);
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
        || event->type() == QEvent::ScreenChangeInternal)) QTimer::singleShot(0, this, &VideoViewportController::synchronize);
    return false;
}
