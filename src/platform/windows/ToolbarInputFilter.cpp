#include "platform/windows/ToolbarInputFilter.h"
#include <QEvent>
#include <QGuiApplication>

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

ToolbarInputFilter::ToolbarInputFilter(QObject* parent) : QObject(parent) {
    if (qGuiApp) {
        qGuiApp->installNativeEventFilter(this);
    }
}

ToolbarInputFilter::~ToolbarInputFilter() {
    if (qGuiApp) {
        qGuiApp->removeNativeEventFilter(this);
    }
}

void ToolbarInputFilter::attach(QWindow* window) {
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
    }
}

bool ToolbarInputFilter::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window_) {
        if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) {
            nativeId_ = window_->winId();
            applyWindowStyles();
        }
    }
    return false;
}

void ToolbarInputFilter::applyWindowStyles() {
#if defined(Q_OS_WIN)
    if (nativeId_) {
        HWND hwnd = reinterpret_cast<HWND>(nativeId_);
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        // 工具栏窗口应用 WS_EX_NOACTIVATE，保证交互时不抢夺前台全屏播放器焦点
        exStyle |= WS_EX_NOACTIVATE;
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, exStyle);
    }
#endif
}

bool ToolbarInputFilter::nativeEventFilter(const QByteArray&, void* message, qintptr* result) {
#if defined(Q_OS_WIN)
    const auto* msg = static_cast<MSG*>(message);
    if (nativeId_ && msg->hwnd == reinterpret_cast<HWND>(nativeId_)) {
        if (msg->message == WM_MOUSEACTIVATE) {
            // 点击工具栏时响应交互，但绝不激活窗口夺取前台焦点
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
