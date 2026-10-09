#include "platform/windows/WindowBackdropController.h"
#include "infrastructure/SettingsStore.h"
#include "infrastructure/LogModel.h"
#include <QAccessibilityHints>
#include <QGuiApplication>
#include <QOperatingSystemVersion>
#include <QPlatformSurfaceEvent>
#include <QStyleHints>
#include <QTimer>
#include <QSurfaceFormat>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <powersetting.h>
#include <winrt/Windows.UI.ViewManagement.h>

WindowBackdropController::WindowBackdropController(SettingsStore* settings, LogModel* logs, QObject* parent)
    : QObject(parent), settings_(settings), logs_(logs) {
    connect(settings_, &SettingsStore::changed, this,
        [this, requested = settings_->values().value("micaEnabled").toBool()]() mutable {
            const bool next = settings_->values().value("micaEnabled").toBool();
            if (next != requested) { requested = next; scheduleRefresh(); }
        });
    connect(qGuiApp->styleHints(), &QStyleHints::colorSchemeChanged, this, &WindowBackdropController::scheduleRefresh);
    connect(qGuiApp->styleHints()->accessibility(), &QAccessibilityHints::contrastPreferenceChanged,
            this, &WindowBackdropController::scheduleRefresh);
    qGuiApp->installNativeEventFilter(this);
}
WindowBackdropController::~WindowBackdropController() {
    if (powerNotification_) UnregisterPowerSettingNotification(powerNotification_);
    qGuiApp->removeNativeEventFilter(this);
}
QString WindowBackdropController::fallback(bool requested, bool nativeWindows, bool supported,
                                           bool highContrast, bool transparent, bool savingPower) {
    if (!requested) return QStringLiteral("已关闭窗口材质");
    if (!nativeWindows) return QStringLiteral("当前图形平台使用纯色背景");
    if (!supported) return QStringLiteral("Mica 需要 Windows 11 22H2 或更新版本");
    if (highContrast) return QStringLiteral("高对比度模式使用纯色背景");
    if (!transparent) return QStringLiteral("系统已关闭透明效果");
    if (savingPower) return QStringLiteral("节能模式使用纯色背景");
    return {};
}
void WindowBackdropController::attach(QWindow* window) {
    Q_ASSERT(!window_);
    window_ = window;
    auto format = window->format();
    format.setAlphaBufferSize(8);
    window->setFormat(format);
    window->installEventFilter(this);
    // Creating the native surface emits SurfaceCreated, without showing the window.
    window->create();
    surfaceReady_ = true;
    nativeId_ = window->winId();
    refresh();
}
bool WindowBackdropController::eventFilter(QObject* watched, QEvent* event) {
    if (watched == window_ && (event->type() == QEvent::Show || event->type() == QEvent::WinIdChange))
        scheduleRefresh();
    if (watched == window_ && event->type() == QEvent::PlatformSurface) {
        const auto type = static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType();
        surfaceReady_ = type == QPlatformSurfaceEvent::SurfaceCreated;
        if (surfaceReady_) scheduleRefresh();
        else {
            nativeId_ = 0;
            if (powerNotification_) UnregisterPowerSettingNotification(powerNotification_);
            powerNotification_ = nullptr;
            publish(false, QStringLiteral("窗口表面正在重建"));
        }
    }
    return false;
}
bool WindowBackdropController::nativeEventFilter(const QByteArray&, void* message, qintptr* result) {
    const auto* msg = static_cast<MSG*>(message);
    if (nativeId_ && msg->hwnd == reinterpret_cast<HWND>(nativeId_)) {
        if (msg->message == WM_ERASEBKGND && effective_) {
        // Black GDI pixels have zero alpha in the DWM extended frame. Only
        // erase the native backing store, never Qt Quick's rendered content.
        RECT rect{};
        GetClientRect(msg->hwnd, &rect);
        FillRect(reinterpret_cast<HDC>(msg->wParam), &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        *result = 1;
        return true;
        }
    }
    if (msg->message == WM_SETTINGCHANGE || msg->message == WM_THEMECHANGED ||
        msg->message == WM_POWERBROADCAST || msg->message == WM_DWMCOMPOSITIONCHANGED)
        scheduleRefresh();
    return false;
}
void WindowBackdropController::scheduleRefresh() {
    if (pending_) return;
    pending_ = true;
    QTimer::singleShot(0, this, [this] { pending_ = false; refresh(); });
}
void WindowBackdropController::publish(bool enabled, const QString& reason) {
    if (effective_ == enabled && reason_ == reason) return;
    effective_ = enabled;
    reason_ = reason;
    if (logs_) logs_->append("INFO", enabled ? QStringLiteral("窗口材质：Mica 路径已启用")
                                             : QStringLiteral("窗口材质：纯色（%1）").arg(reason));
    emit changed();
}
void WindowBackdropController::refresh() {
    if (!window_ || !surfaceReady_) return;
    const bool native = QGuiApplication::platformName() == "windows" &&
                        QQuickWindow::sceneGraphBackend() != "software" &&
                        qEnvironmentVariable("QT_QUICK_BACKEND") != "software" &&
                        QQuickWindow::graphicsApi() == QSGRendererInterface::Direct3D11;
    const auto version = QOperatingSystemVersion::current();
    const bool supported = version.majorVersion() >= 10 && version.microVersion() >= 22621;
    bool transparent = false;
    if (native && supported) {
        try { transparent = winrt::Windows::UI::ViewManagement::UISettings().AdvancedEffectsEnabled(); }
        catch (const winrt::hresult_error&) { /* Unknown system policy: keep an opaque fallback. */ }
    }
    SYSTEM_POWER_STATUS power{};
    const bool saving = GetSystemPowerStatus(&power) && power.SystemStatusFlag == 1;
    QString reason = fallback(settings_->values().value("micaEnabled").toBool(), native, supported,
        qGuiApp->styleHints()->accessibility()->contrastPreference() == Qt::ContrastPreference::HighContrast,
        transparent, saving);
    if (!native || !supported) { publish(false, reason); return; }
    const auto hwnd = reinterpret_cast<HWND>(window_->winId());
    nativeId_ = window_->winId();
    if (!powerNotification_)
        powerNotification_ = RegisterPowerSettingNotification(hwnd, &GUID_POWER_SAVING_STATUS, DEVICE_NOTIFY_WINDOW_HANDLE);
    // Qt enables the legacy blur region for alpha windows. Clear that region
    // before selecting the modern system backdrop, using public DWM APIs only.
    DWM_BLURBEHIND legacy{};
    legacy.dwFlags = DWM_BB_ENABLE;
    legacy.fEnable = FALSE;
    DwmEnableBlurBehindWindow(hwnd, &legacy);
    const BOOL dark = qGuiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    auto type = reason.isEmpty() ? DWMSBT_MAINWINDOW : DWMSBT_NONE;
    HRESULT result = DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
    // Keep the frame extended so that native DWM caption buttons, rounded corners,
    // and shadows stay active even when Mica is disabled (solid color fallback).
    const MARGINS margins = reason.isEmpty() ? MARGINS{-1, -1, -1, -1} : MARGINS{};
    if (SUCCEEDED(result)) result = DwmExtendFrameIntoClientArea(hwnd, &margins);

    constexpr DWORD dwmwaCaptionColor = 35; // DWMWA_CAPTION_COLOR (Windows 11 22000+)
    constexpr DWORD dwmwaTextColor = 36;    // DWMWA_TEXT_COLOR (Windows 11 22000+)
    COLORREF captionColor = reason.isEmpty() ? 0xFFFFFFFF : (dark ? RGB(32, 32, 32) : RGB(243, 243, 243));
    COLORREF textColor = reason.isEmpty() ? 0xFFFFFFFF : (dark ? RGB(255, 255, 255) : RGB(0, 0, 0));
    DwmSetWindowAttribute(hwnd, dwmwaCaptionColor, &captionColor, sizeof(captionColor));
    DwmSetWindowAttribute(hwnd, dwmwaTextColor, &textColor, sizeof(textColor));

    if (FAILED(result)) {
        type = DWMSBT_NONE;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &type, sizeof(type));
        const MARGINS reset{};
        DwmExtendFrameIntoClientArea(hwnd, &reset);
        reason = QStringLiteral("系统材质不可用（0x%1）").arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
    }
    if (reason.isEmpty()) {
        const HDC dc = GetDC(hwnd);
        if (dc) {
            RECT rect{};
            GetClientRect(hwnd, &rect);
            FillRect(dc, &rect, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            ReleaseDC(hwnd, dc);
        }
    }
    publish(reason.isEmpty(), reason);
}
