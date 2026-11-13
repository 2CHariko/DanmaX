#include "platform/windows/WindowLayerController.h"
#include "platform/windows/MediaMonitor.h"
#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QScopedValueRollback>
#include <QStyleHints>
#include <QTimer>
#include <algorithm>
#include <vector>
#include <windows.h>
#include <dwmapi.h>
#include <map>

namespace {
void applyMatchingCaption(quintptr id) {
    if (!id) return;
    const BOOL dark = qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    DwmSetWindowAttribute(reinterpret_cast<HWND>(id), DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
}
// OUTOFCONTEXT callbacks run on the registering GUI thread. Each controller
// owns its hooks; there is no process-wide singleton or cross-thread Qt access.
thread_local std::map<HWINEVENTHOOK, QPointer<WindowLayerController>> eventTargets;
// Raising an owned toolbar must not also raise its owner (the full-screen
// overlay) above the panel. Keep each owner's already reconciled position.
constexpr unsigned positioningFlags = SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER;
quintptr insertion(HWND value) { return reinterpret_cast<quintptr>(value); }
bool above(quintptr upper, quintptr lower) {
    if (!upper || !lower) return false;
    for (auto window = GetWindow(reinterpret_cast<HWND>(lower), GW_HWNDPREV); window;
         window = GetWindow(window, GW_HWNDPREV))
        if (insertion(window) == upper) return true;
    return false;
}
bool nativeTopmost(quintptr id) {
    return GetWindowLongPtrW(reinterpret_cast<HWND>(id), GWL_EXSTYLE) & WS_EX_TOPMOST;
}
bool ourWindow(quintptr value) {
    const auto window = reinterpret_cast<HWND>(value);
    DWORD process{};
    return window && IsWindow(window) && GetWindowThreadProcessId(window, &process) &&
           process == GetCurrentProcessId();
}
quintptr competingTopmostAbove(quintptr overlayId) {
    const auto overlay = reinterpret_cast<HWND>(overlayId);
    RECT bounds{};
    if (!overlay || !GetWindowRect(overlay, &bounds)) return 0;
    for (auto window = GetTopWindow(nullptr); window && window != overlay; window = GetWindow(window, GW_HWNDNEXT)) {
        const auto id = insertion(window);
        if (ourWindow(id) || !IsWindowVisible(window) || IsIconic(window) || !nativeTopmost(id)) continue;
        DWORD cloaked{};
        if (SUCCEEDED(DwmGetWindowAttribute(window, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) continue;
        RECT other{}, overlap{};
        if (GetWindowRect(window, &other) && IntersectRect(&overlap, &bounds, &other)) return id;
    }
    return 0;
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
    if (qGuiApp) connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (QGuiApplication::platformName() == "windows") applyMatchingCaption(handle(matching_));
    });
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
    attachMatching(nullptr);
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
        return nativeTopmost(id);
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
            if (!overlay_ && !matching_) stopMaintenance();
            if (!main_) { baselineHandle_ = 0; modified_ = false; }
            scheduleInvalidation();
        });
    }
}
void WindowLayerController::attachMatching(QWindow* window, std::function<bool(quintptr)> playerMatches) {
    if (matching_ && matching_ != window) {
        disconnect(matching_, nullptr, this, nullptr);
        if (const auto id = handle(matching_); id && nativeTopmost(id)) position(id, insertion(HWND_NOTOPMOST));
    }
    if (window && matching_ != window) connect(window, &QObject::destroyed, this, [this] {
        matchingPlayer_ = {}; invalidate();
    });
    matching_ = window;
    matchingPlayer_ = std::move(playerMatches);
    matchingFailureReported_ = false;
    invalidate();
}
void WindowLayerController::showMatching(QWindow* window, bool activate) {
    if (!window || matching_ != window) return;
    if (QGuiApplication::platformName() == "windows") {
        // Qt::Tool shows without taking focus on Windows. Give this interactive
        // tool a taskbar entry so users can reach it even with the main window hidden.
        const auto hwnd = reinterpret_cast<HWND>(window->winId());
        if (ourWindow(insertion(hwnd))) {
            const auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, (style & ~WS_EX_TOOLWINDOW) | WS_EX_APPWINDOW);
            applyMatchingCaption(insertion(hwnd));
        }
    }
    window->show();
    invalidate();
    if (activate && matchingPlayer_ && matchingPlayer_(insertion(GetForegroundWindow()))) window->requestActivate();
    QPointer<QWindow> shown(window);
    QTimer::singleShot(250, this, [this, shown] {
        if (shown && shown == matching_ && shown->isVisible() && !shown->isActive()) shown->alert(4000);
    });
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
    const bool enabled = (onTop_ && overlayAllowed_ && overlay_ && overlay_->isVisible()) ||
                         (matching_ && matching_->isVisible());
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
    return competingTopmostAbove(group.front()) != 0;
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
    bool managed = window == main_ || window == overlay_ || window == toolbar_ || window == matching_;
    // Native QML tooltip windows are created lazily. Reconcile their first show
    // too, otherwise a newly opened tooltip can rise above an existing dialog.
    for (auto* owner = window->transientParent(); !managed && owner; owner = owner->transientParent())
        managed = owner == main_ || owner == toolbar_ || owner == matching_;
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
        if (type == QEvent::Show && window == overlay_) {
            // Prepare the overlay below the panel before first exposure.
            dirty_ = true;
            reconcile(onTop_, panelForeground_, overlayAllowed_);
        }
        dirty_ = true;
        // QWindow sends Show before the platform shows the HWND. Reconcile
        // again after native visibility (including owned tooltips) has caught up.
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
    if (!changed && ready_ && (!checkOrder || !onTop || !needsRepair(panel))) {
        if (matching_ && (injected_ || QGuiApplication::platformName() == "windows")) {
            QScopedValueRollback guard(reconciling_, true);
            reconcileMatching(foreground, overlayAllowed && toolbar_ && toolbar_->isVisible() ? handle(toolbar_) : 0);
        }
        return ready_;
    }
    QScopedValueRollback guard(reconciling_, true);
    dirty_ = false;
    if (!injected_ && QGuiApplication::platformName() != "windows") return ready_ = true;

    const auto mainId = handle(main_);
    if (mainId != baselineHandle_) {
        baselineHandle_ = mainId;
        baselineTopmost_ = topmost(main_);
        modified_ = false;
    }
    bool success = true, adjusted = false;
    if (!panel || !onTop) {
        adjusted = modified_;
        success = restoreMain();
    }
    if (panel && !modified_) baselineTopmost_ = topmost(main_);

    const auto overlayId = overlayAllowed && overlay_ && overlay_->isVisible() ? handle(overlay_) : 0;
    const auto toolbarId = overlayAllowed && toolbar_ && toolbar_->isVisible() ? handle(toolbar_) : 0;
    const auto competitor = onTop && !injected_ ? competingTopmostAbove(overlayId) : 0;
    const auto move = [&](quintptr window, HWND after) {
        adjusted = true;
        success = position(window, insertion(after)) && success;
    };
    // Demoting an owner can also demote its owned windows. Do it before
    // arranging the toolbar group and leave healthy windows where they are.
    if (overlayId && !onTop && topmost(overlay_)) move(overlayId, HWND_NOTOPMOST);
    quintptr lower = competitor;
    const auto ensureAbove = [&](quintptr id, bool top) {
        if (injected_ || !id || (top && !nativeTopmost(id)) || (lower && !above(id, lower)))
            move(id, top ? HWND_TOPMOST : HWND_TOP);
        lower = id;
    };

    if (panel) {
        modified_ = modified_ || (onTop && !baselineTopmost_);
        ensureAbove(mainId, onTop || baselineTopmost_);
    }

    if (toolbarId) {
        const bool top = onTop || (panel && baselineTopmost_);
        if (!top && nativeTopmost(toolbarId)) move(toolbarId, HWND_NOTOPMOST);
        ensureAbove(toolbarId, top);
        for (const auto& owned : visibleOwnedWindows(toolbarId))
            ensureAbove(owned.handle, top);
    }

    if (panel) {
        // Native dialogs do not necessarily have a QWindow.
        for (const auto& owned : visibleOwnedWindows(mainId))
            ensureAbove(owned.handle, onTop || baselineTopmost_);
    }

    // The lowest window must never be raised above its own controls as an
    // intermediate step. In the same band, insert it directly behind the
    // lowest control; if there are no controls, it can lead the band itself.
    if (overlayId) {
        const auto anchor = panel ? mainId : toolbarId;
        const bool anchorTop = onTop || (panel && baselineTopmost_);
        const bool repair = (onTop && (injected_ || !nativeTopmost(overlayId))) ||
                            (anchor && above(overlayId, anchor)) || (competitor && above(competitor, overlayId));
        if (repair) {
            if (anchor && anchorTop == onTop)
                move(overlayId, reinterpret_cast<HWND>(anchor));
            else if (onTop)
                move(overlayId, HWND_TOPMOST);
        }
    }
    if (matching_) reconcileMatching(foreground, toolbarId ? toolbarId : overlayId);
    ready_ = success;
    if (success && adjusted) emit orderAdjusted();
    if (!success && !failureReported_) {
        failureReported_ = true;
        emit operationFailed(QStringLiteral("窗口层级调整失败；面板操作期间暂时隐藏弹幕并重试"));
    } else if (success) failureReported_ = false;
    return ready_;
}

void WindowLayerController::reconcileMatching(quintptr foreground, quintptr lower) {
    const auto id = handle(matching_);
    if (!id) return;
    const bool shown = matching_->isVisible() && matching_->windowState() != Qt::WindowMinimized;
    const bool player = shown && matchingPlayer_ && matchingPlayer_(foreground);
    const bool relevant = shown && (player || MediaMonitor::windowOrOwnerForeground(id));
    const bool top = onTop_ && relevant;
    bool success = true;
    const auto move = [&](quintptr target, HWND after) { success = position(target, insertion(after)) && success; };
    const auto owned = visibleOwnedWindows(id);
    if (!top) {
        bool demoted = nativeTopmost(id);
        if (demoted) move(id, HWND_NOTOPMOST);
        for (const auto& child : owned) {
            if (!nativeTopmost(child.handle)) continue;
            move(child.handle, HWND_NOTOPMOST); demoted = true;
        }
        if (!relevant && shown && demoted && foreground && !nativeTopmost(foreground)) {
            if (above(id, foreground))
                move(id, reinterpret_cast<HWND>(foreground));
            for (const auto& child : owned) if (above(child.handle, foreground)) move(child.handle, reinterpret_cast<HWND>(foreground));
        }
    }
    if (relevant) {
        if (!lower && overlayAllowed_ && overlay_ && overlay_->isVisible()) lower = handle(overlay_);
        // Respect the toolbar's actual band. With topmost disabled a third-party
        // topmost window cannot be overtaken without changing bands.
        const auto raiseOver = [&](quintptr window, quintptr anchor) {
            if ((top && !nativeTopmost(window)) ||
                (anchor && (top || !nativeTopmost(anchor)) && !above(window, anchor)))
                move(window, top ? HWND_TOPMOST : HWND_TOP);
        };
        if (player) raiseOver(id, foreground);
        if (lower && toolbar_ && handle(toolbar_) == lower) {
            for (const auto& child : visibleOwnedWindows(lower)) raiseOver(id, child.handle);
        }
        raiseOver(id, lower);
        auto anchor = id;
        for (const auto& child : owned) { raiseOver(child.handle, anchor); anchor = child.handle; }
    }
    if (!success && !matchingFailureReported_)
        emit operationFailed(QStringLiteral("手动匹配窗口层级调整失败，将继续重试"));
    matchingFailureReported_ = !success;
}
