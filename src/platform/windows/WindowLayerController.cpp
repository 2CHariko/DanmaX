#include "platform/windows/WindowLayerController.h"
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QScopedValueRollback>
#include <QTimer>
#include <algorithm>
#include <vector>
#include <windows.h>
#include <dwmapi.h>
#include <map>

namespace {
// OUTOFCONTEXT callbacks run on the registering GUI thread. Each controller
// owns its hooks; there is no process-wide singleton or cross-thread Qt access.
thread_local std::map<HWINEVENTHOOK, QPointer<WindowLayerController>> eventTargets;
// Raising an owned toolbar must not also raise its owner (the full-screen
// overlay) above the panel. Keep each owner's already reconciled position.
constexpr unsigned positioningFlags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER;
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
    if (qGuiApp) qGuiApp->installEventFilter(this);
    maintenanceTimer_.setInterval(100);
    maintenanceTimer_.setParent(this);
    maintenanceTimer_.setObjectName(QStringLiteral("windowLayerWatchdog"));
    eventTimer_.setSingleShot(true);
    eventTimer_.setInterval(16);
    eventTimer_.setParent(this);
    eventTimer_.setObjectName(QStringLiteral("windowLayerEvents"));
    connect(&maintenanceTimer_, &QTimer::timeout, this, &WindowLayerController::maintain);
    connect(&eventTimer_, &QTimer::timeout, this, &WindowLayerController::maintain);
}

WindowLayerController::~WindowLayerController() {
    stopMaintenance();
    if (qGuiApp) qGuiApp->removeEventFilter(this);
    restoreMain();
}

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
    // Restore the entire panel owner group to its original band. Owner isolation
    // is only needed while arranging the interleaved panel/toolbar groups.
    if (!positioner_(baselineHandle_, insertion(baselineTopmost_ ? HWND_TOPMOST : HWND_NOTOPMOST),
                     positioningFlags & ~SWP_NOOWNERZORDER))
        return false;
    modified_ = false;
    return true;
}

void WindowLayerController::attach(QWindow* main, QWindow* overlay, QWindow* toolbar) {
    stopMaintenance();
    restoreMain();
    for (auto* window : {main_.data(), overlay_.data(), toolbar_.data()}) {
        if (!window) continue;
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
        connect(window, &QObject::destroyed, this, [this] {
            dirty_ = true;
            if (!overlay_) stopMaintenance();
            if (!main_) { baselineHandle_ = 0; modified_ = false; }
            scheduleInvalidation();
        });
    }
}

void WindowLayerController::stopMaintenance() {
    maintenanceTimer_.stop();
    eventTimer_.stop();
    for (const auto id : eventHooks_) {
        const auto hook = reinterpret_cast<HWINEVENTHOOK>(id);
        eventTargets.erase(hook);
        UnhookWinEvent(hook);
    }
    eventHooks_.clear();
}

void WindowLayerController::updateMaintenance() {
    if (!injected_ && QGuiApplication::platformName() != "windows") { stopMaintenance(); return; }
    const bool enabled = onTop_ && overlayAllowed_ && overlay_ && overlay_->isVisible();
    if (!enabled) { stopMaintenance(); return; }
    if (maintenanceTimer_.isActive()) return;
    maintenanceTimer_.start();
    if (injected_ || QGuiApplication::platformName() != "windows") return;
    bool complete = true;
    for (const DWORD event : {DWORD(EVENT_SYSTEM_FOREGROUND), DWORD(EVENT_SYSTEM_MINIMIZEEND),
                              DWORD(EVENT_OBJECT_SHOW), DWORD(EVENT_OBJECT_REORDER)}) {
        const auto hook = SetWinEventHook(event, event, nullptr,
            [](HWINEVENTHOOK hook, DWORD event, HWND window, LONG object, LONG, DWORD, DWORD) {
                const auto target = eventTargets.find(hook);
                if (target == eventTargets.end() || !target->second) return;
                if (event >= EVENT_OBJECT_CREATE && object != OBJID_WINDOW) return;
                if (!window || !IsWindow(window)) return;
                auto* self = target->second.data();
                if (!self->eventTimer_.isActive()) self->eventTimer_.start();
            }, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
        if (hook) {
            eventTargets.emplace(hook, this);
            eventHooks_.push_back(reinterpret_cast<quintptr>(hook));
        } else complete = false;
    }
    if (!complete)
        emit operationFailed(QStringLiteral("窗口事件监听未全部建立；继续使用周期层级检查"));
}

void WindowLayerController::maintain() {
    // Refresh policy on an actual foreground transition, without forcing the
    // media adapter's foreground lookup on every healthy watchdog tick.
    QPointer<WindowLayerController> alive(this);
    if (reinterpret_cast<quintptr>(GetForegroundWindow()) != foregroundHandle_) emit invalidated();
    if (!alive || !maintenanceTimer_.isActive()) return;
    const bool wasReady = ready_;
    reconcile(onTop_, panelForeground_, overlayAllowed_, true);
    if (alive && wasReady != ready_) emit invalidated();
}

bool WindowLayerController::needsRepair(bool panel) const {
    if (injected_) return true; // Injected positioners need not change native state.
    std::vector<quintptr> group{handle(overlay_)}; // bottom to top
    if (panel) group.push_back(handle(main_));
    if (toolbar_ && toolbar_->isVisible()) {
        group.push_back(handle(toolbar_));
        for (const auto& owned : visibleOwnedWindows(group.back())) group.push_back(owned.handle);
    }
    if (panel)
        for (const auto& owned : visibleOwnedWindows(handle(main_))) group.push_back(owned.handle);

    std::vector<HWND> windows;
    EnumWindows([](HWND window, LPARAM data) -> BOOL {
        reinterpret_cast<std::vector<HWND>*>(data)->push_back(window);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows));
    auto previous = windows.end();
    for (const auto id : group) {
        const auto window = reinterpret_cast<HWND>(id);
        if (!ourWindow(id) || !(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST)) return true;
        const auto current = std::find(windows.begin(), windows.end(), window);
        if (current == windows.end() || (previous != windows.end() && current >= previous)) return true;
        previous = current;
    }
    // Only competing, visible topmost windows above the overlay and on its
    // monitor area matter. A topmost flag alone does not prove correct Z order.
    const auto overlay = reinterpret_cast<HWND>(group.front());
    RECT bounds{};
    if (!GetWindowRect(overlay, &bounds)) return true;
    for (const auto window : windows) {
        if (window == overlay) break;
        if (ourWindow(reinterpret_cast<quintptr>(window)) || !IsWindowVisible(window) || IsIconic(window) ||
            !(GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOPMOST)) continue;
        DWORD cloaked{};
        if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
        RECT other{}, overlap{};
        if (GetWindowRect(window, &other) && IntersectRect(&overlap, &bounds, &other)) return true;
    }
    return false;
}

void WindowLayerController::scheduleInvalidation() {
    if (queued_) return;
    queued_ = true;
    QTimer::singleShot(0, this, [this] { queued_ = false; emit invalidated(); });
}

void WindowLayerController::invalidate() {
    dirty_ = true;
    scheduleInvalidation();
}

bool WindowLayerController::eventFilter(QObject* watched, QEvent* event) {
    const auto type = event->type();
    if (type != QEvent::PlatformSurface && type != QEvent::Show && type != QEvent::Hide &&
        type != QEvent::WinIdChange && type != QEvent::WindowStateChange) return false;
    auto* window = qobject_cast<QWindow*>(watched);
    if (!window) return false;
    bool managed = window == main_ || window == overlay_ || window == toolbar_;
    // Native QML tooltip windows are created lazily. Reconcile their first show
    // too, otherwise a newly opened tooltip can rise above an existing dialog.
    for (auto* owner = window->transientParent(); !managed && owner; owner = owner->transientParent())
        managed = owner == main_ || owner == toolbar_;
    if (!managed) return false;
    if (event->type() == QEvent::PlatformSurface) {
        const auto surfaceType = static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
        if (watched == main_ && surfaceType == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
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
            reconcile(onTop_, panelForeground_, overlayAllowed_);
        scheduleInvalidation();
    }
    return false;
}

bool WindowLayerController::reconcile(bool onTop, bool panelForeground, bool overlayAllowed, bool checkOrder) {
    const auto foreground = reinterpret_cast<quintptr>(GetForegroundWindow());
    const bool changed = dirty_ || onTop != onTop_ || panelForeground != panelForeground_ ||
                         overlayAllowed != overlayAllowed_ || (panelForeground && foreground != foregroundHandle_);
    onTop_ = onTop;
    panelForeground_ = panelForeground;
    overlayAllowed_ = overlayAllowed;
    foregroundHandle_ = foreground;
    if (reconciling_) { dirty_ = true; return ready_; }
    updateMaintenance();
    const bool panel = panelForeground && overlayAllowed && main_ && main_->isVisible() &&
                       main_->windowState() != Qt::WindowMinimized;
    if (!changed && ready_ && (!checkOrder || !onTop || !needsRepair(panel))) return ready_;
    QScopedValueRollback guard(reconciling_, true);
    dirty_ = false;
    if (!injected_ && QGuiApplication::platformName() != "windows") return ready_ = true;

    const auto mainId = handle(main_);
    if (mainId != baselineHandle_) {
        baselineHandle_ = mainId;
        baselineTopmost_ = topmost(main_);
        modified_ = false;
    }
    bool success = true;
    if (!panel || !onTop) success = restoreMain();
    if (panel && !modified_) baselineTopmost_ = topmost(main_);

    if (overlayAllowed && overlay_ && overlay_->isVisible()) {
        const auto overlayId = handle(overlay_);
        if (onTop)
            success = position(overlayId, insertion(HWND_TOPMOST)) && success;
        if (!onTop && topmost(overlay_))
            success = position(overlayId, insertion(HWND_NOTOPMOST)) && success;
    }

    if (panel) {
        modified_ = modified_ || (onTop && !baselineTopmost_);
        const auto after = insertion(onTop || baselineTopmost_ ? HWND_TOPMOST : HWND_TOP);
        success = position(mainId, after) && success;
    }

    // Raise the toolbar after the panel so it remains usable with a maximized
    // panel. Its native tips/popups follow it; panel dialogs retain precedence.
    if (overlayAllowed && toolbar_ && toolbar_->isVisible()) {
        const auto toolbarId = handle(toolbar_);
        if (!onTop && !(panel && baselineTopmost_) && topmost(toolbar_))
            success = position(toolbarId, insertion(HWND_NOTOPMOST)) && success;
        const auto after = insertion(onTop || (panel && baselineTopmost_) ? HWND_TOPMOST : HWND_TOP);
        success = position(toolbarId, after) && success;
        for (const auto& owned : visibleOwnedWindows(toolbarId))
            success = position(owned.handle, after) && success;
    }

    if (panel) {
        const auto after = insertion(onTop || baselineTopmost_ ? HWND_TOPMOST : HWND_TOP);
        // Native dialogs do not necessarily have a QWindow.
        for (const auto& owned : visibleOwnedWindows(mainId))
            success = position(owned.handle, after) && success;
    }
    ready_ = success;
    if (success) emit orderAdjusted();
    if (!success && !failureReported_) {
        failureReported_ = true;
        emit operationFailed(QStringLiteral("窗口层级调整失败；面板操作期间暂时隐藏弹幕并重试"));
    } else if (success) failureReported_ = false;
    return ready_;
}
