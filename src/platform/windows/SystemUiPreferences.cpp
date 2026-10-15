#include "SystemUiPreferences.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <windows.h>
#include <utility>

SystemUiPreferences::SystemUiPreferences(QObject* parent, std::function<bool()> query)
    : QObject(parent), query_(std::move(query)) {
    if (!query_) query_ = [] {
        BOOL enabled = TRUE;
        return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0) || enabled != FALSE;
    };
    refresh();
    if (auto* app = QCoreApplication::instance()) app->installNativeEventFilter(this);
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        connect(app, &QGuiApplication::applicationStateChanged, this, &SystemUiPreferences::refresh);
}
SystemUiPreferences::~SystemUiPreferences() {
    if (auto* app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
}
void SystemUiPreferences::refresh() {
    const bool enabled = query_();
    if (animationsEnabled_ == enabled) return;
    animationsEnabled_ = enabled;
    emit changed();
}
bool SystemUiPreferences::nativeEventFilter(const QByteArray&, void* message, qintptr*) {
    const auto* msg = static_cast<MSG*>(message);
    if (msg->message == WM_SETTINGCHANGE || msg->message == WM_THEMECHANGED) refresh();
    return false;
}
