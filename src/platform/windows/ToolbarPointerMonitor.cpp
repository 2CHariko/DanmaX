#include "platform/windows/ToolbarPointerMonitor.h"
#include <QCursor>
#include <algorithm>
#include <utility>

ToolbarPointerMonitor::ToolbarPointerMonitor(QObject* parent, CursorQuery cursorQuery)
    : QObject(parent), cursorQuery_(cursorQuery ? std::move(cursorQuery) : CursorQuery([] { return QCursor::pos(); })) {
    timer_.setInterval(50);
    connect(&timer_, &QTimer::timeout, this, &ToolbarPointerMonitor::sample);
}

void ToolbarPointerMonitor::attach(QWindow* overlay, QWindow* toolbar) {
    if (overlay_) disconnect(overlay_, nullptr, this, nullptr);
    if (toolbar_) disconnect(toolbar_, nullptr, this, nullptr);
    overlay_ = overlay;
    toolbar_ = toolbar;
    for (auto* window : {overlay, toolbar}) {
        if (!window) continue;
        connect(window, &QWindow::xChanged, this, &ToolbarPointerMonitor::sample);
        connect(window, &QWindow::yChanged, this, &ToolbarPointerMonitor::sample);
        connect(window, &QWindow::widthChanged, this, &ToolbarPointerMonitor::sample);
        connect(window, &QWindow::heightChanged, this, &ToolbarPointerMonitor::sample);
        connect(window, &QWindow::visibleChanged, this, &ToolbarPointerMonitor::sample);
        connect(window, &QObject::destroyed, this, [this] { setEnabled(false); });
    }
    sample();
}

QRect ToolbarPointerMonitor::activationRect() const {
    if (!overlay_) return {};
    const auto geometry = overlay_->geometry();
    const auto width = std::min(activationWidth_, geometry.width());
    if (width <= 0 || geometry.height() <= 0) return {};
    return {geometry.x() + qRound((geometry.width() - width) / 2.0), geometry.y(), width, 10};
}

void ToolbarPointerMonitor::setActivationWidth(int width) {
    width = std::max(0, width);
    if (activationWidth_ == width) return;
    activationWidth_ = width;
    sample();
    emit activationWidthChanged();
}

void ToolbarPointerMonitor::setEnabled(bool enabled) {
    enabled = enabled && overlay_;
    if (enabled == this->enabled()) return;
    if (enabled) timer_.start();
    else timer_.stop();
    // Update the sample before notifying QML that presentation is allowed again.
    sample();
    emit enabledChanged();
}

void ToolbarPointerMonitor::sample() {
    if (!enabled() || !overlay_) {
        setHovered(false);
        return;
    }
    const auto point = cursorQuery_();
    setHovered(activationRect().contains(point) ||
               (toolbar_ && toolbar_->isVisible() && toolbar_->geometry().contains(point)));
}

void ToolbarPointerMonitor::setHovered(bool hovered) {
    if (hovered_ == hovered) return;
    hovered_ = hovered;
    emit hoveredChanged();
}
