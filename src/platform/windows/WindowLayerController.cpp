#include "platform/windows/WindowLayerController.h"
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QScopedValueRollback>
#include <QTimer>
#include <algorithm>
#include <vector>
#include <windows.h>

namespace {
constexpr unsigned positioningFlags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE;
quintptr insertion(HWND value) { return reinterpret_cast<quintptr>(value); }
bool ourWindow(quintptr value) {
    const auto window = reinterpret_cast<HWND>(value);
    DWORD process{};
    return window && IsWindow(window) && GetWindowThreadProcessId(window, &process) &&
           process == GetCurrentProcessId();
}
struct OwnedWindow { quintptr handle; int depth; };
std::vector<OwnedWindow> visibleOwnedWindows(quintptr main) {
    struct Scan { quintptr main; std::vector<OwnedWindow> windows; } scan{main, {}};
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        auto& scan = *reinterpret_cast<Scan*>(data);
        if (!IsWindowVisible(window) || !ourWindow(reinterpret_cast<quintptr>(window))) return TRUE;
        auto owner = GetWindow(window, GW_OWNER);
        for (int depth = 1; owner && depth <= 64; ++depth) {
            if (reinterpret_cast<quintptr>(owner) == scan.main) {
                scan.windows.push_back({reinterpret_cast<quintptr>(window), depth});
                break;
            }
            owner = GetWindow(owner, GW_OWNER);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&scan));
    // EnumWindows visits top-to-bottom; preserve sibling order while raising
    // owners first, then descendants. This includes non-foreground native popups.
    std::reverse(scan.windows.begin(), scan.windows.end());
    std::stable_sort(scan.windows.begin(), scan.windows.end(),
                     [](const auto& a, const auto& b) { return a.depth < b.depth; });
    return scan.windows;
}
}

WindowLayerController::WindowLayerController(QObject* parent, Positioner positioner)
    : QObject(parent), positioner_(std::move(positioner)), injected_(bool(positioner_)) {
    if (!positioner_) positioner_ = [](quintptr window, quintptr after, unsigned flags) {
        return ourWindow(window) && SetWindowPos(reinterpret_cast<HWND>(window),
            reinterpret_cast<HWND>(after), 0, 0, 0, 0, flags);
    };
}

WindowLayerController::~WindowLayerController() { restoreMain(); }

quintptr WindowLayerController::handle(QWindow* window) const {
    // winId() creates a surface; never recreate one from an about-to-destroy event.
    if (!window || !window->handle()) return 0;
    const auto id = window->winId();
    return injected_ || ourWindow(id) ? id : 0;
}

bool WindowLayerController::topmost(QWindow* window) const {
    const auto id = handle(window);
    if (ourWindow(id))
        return GetWindowLongPtrW(reinterpret_cast<HWND>(id), GWL_EXSTYLE) & WS_EX_TOPMOST;
    return window && window->flags().testFlag(Qt::WindowStaysOnTopHint);
}

bool WindowLayerController::position(quintptr window, quintptr after) {
    return window && positioner_(window, after, positioningFlags);
}

bool WindowLayerController::restoreMain() {
    if (!modified_) return true;
    if (!main_ || handle(main_) != baselineHandle_) {
        modified_ = false;
        baselineHandle_ = 0;
        return true;
    }
    if (!position(baselineHandle_, insertion(baselineTopmost_ ? HWND_TOPMOST : HWND_NOTOPMOST)))
        return false;
    modified_ = false;
    return true;
}

void WindowLayerController::attach(QWindow* main, QWindow* overlay, QWindow* toolbar) {
    restoreMain();
    for (auto* window : {main_.data(), overlay_.data(), toolbar_.data()}) {
        if (!window) continue;
        window->removeEventFilter(this);
        disconnect(window, nullptr, this, nullptr);
    }
    main_ = main;
    overlay_ = overlay;
    toolbar_ = toolbar;
    baselineHandle_ = 0;
    modified_ = false;
    dirty_ = true;
    for (auto* window : {main, overlay, toolbar}) {
        if (!window) continue;
        window->installEventFilter(this);
        connect(window, &QObject::destroyed, this, [this] {
            dirty_ = true;
            if (!main_) { baselineHandle_ = 0; modified_ = false; }
            scheduleInvalidation();
        });
    }
}

void WindowLayerController::scheduleInvalidation() {
    if (queued_) return;
    queued_ = true;
    QTimer::singleShot(0, this, [this] { queued_ = false; emit invalidated(); });
}

bool WindowLayerController::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::PlatformSurface) {
        const auto type = static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
        if (watched == main_ && type == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            restoreMain();
            baselineHandle_ = 0;
            modified_ = false;
        }
        dirty_ = true;
        scheduleInvalidation();
    } else if (event->type() == QEvent::Show || event->type() == QEvent::Hide ||
               event->type() == QEvent::WinIdChange || event->type() == QEvent::WindowStateChange) {
        dirty_ = true;
        // Show is synchronous: correct Z order before the first rendered frame.
        if (event->type() == QEvent::Show)
            reconcile(strategy_, panelForeground_, overlayAllowed_);
        scheduleInvalidation();
    }
    return false;
}

bool WindowLayerController::reconcile(int strategy, bool panelForeground, bool overlayAllowed, bool periodic) {
    strategy = std::clamp(strategy, 0, 3);
    const auto foreground = reinterpret_cast<quintptr>(GetForegroundWindow());
    const bool changed = dirty_ || strategy != strategy_ || panelForeground != panelForeground_ ||
                         overlayAllowed != overlayAllowed_ || (panelForeground && foreground != foregroundHandle_);
    const bool strategyChanged = strategy != strategy_;
    strategy_ = strategy;
    panelForeground_ = panelForeground;
    overlayAllowed_ = overlayAllowed;
    foregroundHandle_ = foreground;
    if (reconciling_) { dirty_ = true; return ready_; }
    // Strategies 0/1 have no periodic raise; only 2/3 actively keep the group on top.
    if (!changed && ready_ && (!periodic || strategy < 2)) return ready_;
    QScopedValueRollback guard(reconciling_, true);
    dirty_ = false;
    if (!injected_ && QGuiApplication::platformName() != "windows") return ready_ = true;

    const auto mainId = handle(main_);
    if (mainId != baselineHandle_) {
        baselineHandle_ = mainId;
        baselineTopmost_ = topmost(main_);
        modified_ = false;
    }
    const bool panel = panelForeground && overlayAllowed && main_ && main_->isVisible() &&
                       main_->windowState() != Qt::WindowMinimized;
    bool success = true;
    if (!panel || strategy == 0) success = restoreMain();
    if (panel && !modified_) baselineTopmost_ = topmost(main_);

    if (overlayAllowed && overlay_ && overlay_->isVisible()) {
        const auto overlayId = handle(overlay_);
        if (strategy == 3 && (periodic || strategyChanged))
            success = position(overlayId, insertion(HWND_NOTOPMOST)) && success;
        if (strategy > 1 || (strategy == 1 && !topmost(overlay_)))
            success = position(overlayId, insertion(HWND_TOPMOST)) && success;
        if (strategy == 0 && topmost(overlay_))
            success = position(overlayId, insertion(HWND_NOTOPMOST)) && success;
        if (!panel && toolbar_ && toolbar_->isVisible()) {
            const auto toolbarId = handle(toolbar_);
            success = position(toolbarId, insertion(strategy > 0 ? HWND_TOPMOST : HWND_TOP)) && success;
        }
    }

    if (panel) {
        modified_ = modified_ || (strategy > 0 && !baselineTopmost_);
        const auto after = insertion(strategy > 0 || baselineTopmost_ ? HWND_TOPMOST : HWND_TOP);
        success = position(mainId, after) && success;
        // Native dialogs do not necessarily have a QWindow.
        for (const auto& owned : visibleOwnedWindows(mainId))
            success = position(owned.handle, after) && success;
    }
    ready_ = success;
    if (!success && !failureReported_) {
        failureReported_ = true;
        emit operationFailed(QStringLiteral("窗口层级调整失败；面板操作期间暂时隐藏弹幕并重试"));
    } else if (success) failureReported_ = false;
    return ready_;
}
