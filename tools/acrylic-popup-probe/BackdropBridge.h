#pragma once
#include <QAbstractNativeEventFilter>
#include <QGuiApplication>
#include <QHash>
#include <QOperatingSystemVersion>
#include <QPlatformSurfaceEvent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStyleHints>
#include <QAccessibilityHints>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>
#include <windows.h>
#include <dwmapi.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <QWKQuick/quickwindowagent.h>

// Isolated probe only. Uses the already approved QWindowKit through its public API.
class BackdropBridge final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(bool requested READ requested WRITE setRequested NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int windows READ windows NOTIFY changed)
public:
    struct State { WId id{}; bool accepted{}; HRESULT result{E_FAIL}; int readback{}; };
    explicit BackdropBridge(QObject* parent = nullptr) : QObject(parent) {
        qGuiApp->installNativeEventFilter(this);
        connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this, &BackdropBridge::refreshAll);
        connect(qGuiApp->styleHints()->accessibility(), &QAccessibilityHints::contrastPreferenceChanged, this, &BackdropBridge::refreshAll);
    }
    ~BackdropBridge() override { qGuiApp->removeNativeEventFilter(this); }
    bool requested() const { return requested_; }
    bool nativeStyleContrast() const { return nativeContrast_; }
    bool useWindowKit() const { return useWindowKit_; }
    int revision() const { return revision_; }
    int windows() const { return states_.size(); }
    void setRequested(bool value) { if (requested_ == value) return; requested_ = value; refreshAll(); }
    Q_INVOKABLE bool effective(QQuickWindow* window) const { return states_.value(window).accepted; }
    void setHost(QQuickWindow* window) { host_ = window; window->installEventFilter(this); }
    void watchTopLevel(QQuickWindow* window) { track(window, true); }
    Q_INVOKABLE void watch(QQuickItem* item) {
        connect(item, &QQuickItem::windowChanged, this, [this](QQuickWindow* window) { track(window); });
        track(item->window());
    }
    QJsonArray report() const {
        QJsonArray result;
        for (auto it = states_.cbegin(); it != states_.cend(); ++it) result.append(QJsonObject{
            {"hwnd", QString::number(it->id, 16)}, {"accepted", it->accepted}, {"readback", it->readback},
            {"hresult", QString::number(static_cast<quint32>(it->result), 16)},
            {"alphaBits", it.key()->format().alphaBufferSize()}, {"visible", it.key()->isVisible()},
            {"clearColor", it.key()->color().name(QColor::HexArgb)},
            {"active", it.key()->isActive()}, {"owner", QString::number(reinterpret_cast<quintptr>(GetWindow(reinterpret_cast<HWND>(it->id), GW_OWNER)), 16)},
            {"noRedirectionBitmap", bool(GetWindowLongPtrW(reinterpret_cast<HWND>(it->id), GWL_EXSTYLE) & WS_EX_NOREDIRECTIONBITMAP)},
            {"windowKitSetup", agents_.contains(it.key())},
            {"width", it.key()->width()}, {"height", it.key()->height()}});
        return result;
    }
signals:
    void changed();
protected:
    bool eventFilter(QObject* object, QEvent* event) override {
        auto* window = qobject_cast<QQuickWindow*>(object);
        if (window == host_ && event->type() == QEvent::PlatformSurface
            && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) {
            // Detach our native owner override before the host destroys owned HWNDs.
            for (const auto& state : states_) if (state.id)
                SetWindowLongPtrW(reinterpret_cast<HWND>(state.id), GWLP_HWNDPARENT, 0);
        }
        if (!states_.contains(window)) return false;
        if (event->type() == QEvent::PlatformSurface) {
            const auto type = static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
            if (type == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed) { states_[window] = {}; notify(); }
            else schedule(window);
        } else if (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange) schedule(window);
        return false;
    }
    bool nativeEventFilter(const QByteArray&, void* message, qintptr* result) override {
        const auto* msg = static_cast<MSG*>(message);
        if (msg->message == WM_ERASEBKGND) for (const auto& state : states_) {
            if (state.accepted && state.id == reinterpret_cast<WId>(msg->hwnd)) {
                RECT rect{}; GetClientRect(msg->hwnd, &rect);
                FillRect(reinterpret_cast<HDC>(msg->wParam), &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                *result = 1; return true;
            }
        }
        if (msg->message == WM_SETTINGCHANGE || msg->message == WM_THEMECHANGED || msg->message == WM_POWERBROADCAST)
            QTimer::singleShot(0, this, &BackdropBridge::refreshAll);
        return false;
    }
private:
    void notify() { ++revision_; emit changed(); }
    void track(QQuickWindow* window, bool explicitTopLevel = false) {
        if (!window || (!explicitTopLevel && (window->flags() & Qt::WindowType_Mask) != Qt::Popup) || states_.contains(window)) return;
        states_.insert(window, {}); window->installEventFilter(this);
        window->setColor(Qt::transparent);
        if (host_ && !window->transientParent()) window->setTransientParent(host_);
        if (useWindowKit_) {
            auto* agent = new QWK::QuickWindowAgent(window);
            if (agent->setup(window)) agents_.insert(window, agent);
            else agent->deleteLater();
        }
        connect(window, &QObject::destroyed, this, [this, window] { states_.remove(window); initialStyles_.remove(window); agents_.remove(window); notify(); });
        schedule(window);
    }
    void schedule(QQuickWindow* window) {
        QTimer::singleShot(0, this, [this, guard = QPointer<QQuickWindow>(window)] { if (guard) refresh(guard); });
    }
    void refreshAll() { for (auto* window : states_.keys()) refresh(window); notify(); }
    void refresh(QQuickWindow* window) {
        if (!window->handle()) return;
        const auto hwnd = reinterpret_cast<HWND>(window->winId());
        // Explicit native ownership is part of this experiment; Qt's Popup
        // flags on this platform leave GW_OWNER empty even with transientParent.
        if (nativeContrast_ && host_ && !GetWindow(hwnd, GW_OWNER))
            SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, static_cast<LONG_PTR>(host_->winId()));
        bool transparent = false;
        try { transparent = winrt::Windows::UI::ViewManagement::UISettings().AdvancedEffectsEnabled(); } catch (...) {}
        SYSTEM_POWER_STATUS power{};
        const bool saving = GetSystemPowerStatus(&power) && power.SystemStatusFlag == 1;
        const bool supported = QOperatingSystemVersion::current().microVersion() >= 22621;
        const bool native = QGuiApplication::platformName() == "windows" && QQuickWindow::graphicsApi() == QSGRendererInterface::Direct3D11
            && QQuickWindow::sceneGraphBackend() != "software";
        const bool enabled = requested_ && supported && native && transparent && !saving
            && qGuiApp->styleHints()->accessibility()->contrastPreference() != Qt::ContrastPreference::HighContrast;
        // A/B experiment: Qt's layered popup disables DWM non-client rendering.
        // Keep this native style override confined to the probe.
        if (nativeContrast_) {
            const auto currentStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
            if (!initialStyles_.contains(window)) initialStyles_.insert(window, currentStyle);
            const auto desiredStyle = enabled ? (currentStyle & ~LONG_PTR(WS_EX_LAYERED)) : initialStyles_.value(window);
            if (currentStyle != desiredStyle) {
                SetWindowLongPtrW(hwnd, GWL_EXSTYLE, desiredStyle);
                SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            }
            const auto policy = enabled ? DWMNCRP_ENABLED : DWMNCRP_USEWINDOWSTYLE;
            DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
        }
        const BOOL dark = qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
        const auto type = enabled ? DWMSBT_TRANSIENTWINDOW : DWMSBT_NONE;
        HRESULT hr{E_FAIL};
        if (auto* agent = agents_.value(window)) {
            agent->setWindowAttribute(QStringLiteral("dark-mode"), bool(dark));
            hr = agent->setWindowAttribute(QStringLiteral("acrylic-material"), enabled) ? S_OK : E_FAIL;
            // QWindowKit restores AUTO when disabled; this probe needs an explicit NONE for the fallback comparison.
            if (!enabled) DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
        } else {
            DWM_BLURBEHIND legacy{}; legacy.dwFlags = DWM_BB_ENABLE; DwmEnableBlurBehindWindow(hwnd, &legacy);
            hr = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
            const MARGINS margins = enabled ? MARGINS{-1, -1, -1, -1} : MARGINS{};
            if (SUCCEEDED(hr)) hr = DwmExtendFrameIntoClientArea(hwnd, &margins);
        }
        const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));
        int readback{}; DwmGetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &readback, sizeof(readback));
        const bool accepted = enabled && SUCCEEDED(hr) && readback == DWMSBT_TRANSIENTWINDOW;
        states_[window] = {reinterpret_cast<WId>(hwnd), accepted, hr, readback}; notify(); window->requestUpdate();
    }
    bool requested_{true}; int revision_{};
    const bool nativeContrast_{qEnvironmentVariableIsSet("DANMAKU_ACRYLIC_NATIVE_CONTRAST")};
    const bool useWindowKit_{qEnvironmentVariable("DANMAKU_ACRYLIC_MODE") == "QWindowKit"
        || qEnvironmentVariable("DANMAKU_ACRYLIC_MODE") == "Combined"};
    QPointer<QQuickWindow> host_;
    QHash<QQuickWindow*, State> states_;
    QHash<QQuickWindow*, LONG_PTR> initialStyles_;
    QHash<QQuickWindow*, QWK::QuickWindowAgent*> agents_;
};
