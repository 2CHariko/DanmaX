#include "platform/windows/OverlayInputFilter.h"
#include <QEvent>
#include <QGuiApplication>
#include <cmath>

#if defined(Q_OS_WIN)
#include <windows.h>
#include <windowsx.h>
#endif

OverlayInputFilter::OverlayInputFilter(QObject* parent) : QObject(parent) {
    if (qGuiApp) {
        qGuiApp->installNativeEventFilter(this);
    }
}

OverlayInputFilter::~OverlayInputFilter() {
    if (qGuiApp) {
        qGuiApp->removeNativeEventFilter(this);
    }
}

void OverlayInputFilter::attach(QWindow* window) {
    if (window_ == window)
        return;
    if (window_)
        window_->removeEventFilter(this);
    window_ = window;
    if (window_) {
        window_->installEventFilter(this);
        if (window_->isVisible() || window_->winId() != 0) {
            nativeId_ = window_->winId();
            applyWindowStyles();
        }
    } else {
        nativeId_ = 0;
        if (hovering_) {
            hovering_ = false;
            emit hoverChanged(false);
        }
    }
}

void OverlayInputFilter::setInteractiveRects(const QList<QRect>& rects) {
    interactiveRects_ = rects;
}

bool OverlayInputFilter::hitTest(const QPoint& localPos) const {
    for (const auto& rect : interactiveRects_) {
        if (rect.contains(localPos))
            return true;
    }
    return false;
}
bool OverlayInputFilter::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window_) {
        if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
            nativeId_ = window_->winId();
            applyWindowStyles();
        }
    }
    return false;
}

void OverlayInputFilter::applyWindowStyles() {
#if defined(Q_OS_WIN)
    if (nativeId_) {
        HWND hwnd = reinterpret_cast<HWND>(nativeId_);
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        // 清除静态穿透标记（改为通过 WM_NCHITTEST 动态判断），并增加无激活标记，保护前台全屏焦点
        exStyle &= ~WS_EX_TRANSPARENT;
        exStyle |= WS_EX_NOACTIVATE;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    }
#endif
}

bool OverlayInputFilter::nativeEventFilter(const QByteArray&, void* message, qintptr* result) {
#if defined(Q_OS_WIN)
    const auto* msg = static_cast<MSG*>(message);
    if (nativeId_ && msg->hwnd == reinterpret_cast<HWND>(nativeId_)) {
        if (msg->message == WM_NCHITTEST) {
            const int x = GET_X_LPARAM(msg->lParam);
            const int y = GET_Y_LPARAM(msg->lParam);
            POINT pt{x, y};
            ScreenToClient(msg->hwnd, &pt);

            const qreal dpr = window_ ? window_->devicePixelRatio() : 1.0;
            const QPoint localPos(
                static_cast<int>(std::floor(pt.x / (dpr > 0.0 ? dpr : 1.0))),
                static_cast<int>(std::floor(pt.y / (dpr > 0.0 ? dpr : 1.0))));

            const bool hit = hitTest(localPos);
            if (hit != hovering_) {
                hovering_ = hit;
                emit hoverChanged(hovering_);
            }
            if (hit) {
                *result = HTCLIENT;
            } else {
                *result = HTTRANSPARENT;
            }
            return true;
        }

        if (msg->message == WM_MOUSEACTIVATE) {
            // 允许响应工具栏点击，但绝不激活窗口夺取前台焦点
            *result = MA_NOACTIVATE;
            return true;
        }
    }
#else
    Q_UNUSED(message);
    Q_UNUSED(result);
#endif
    return false;
}
