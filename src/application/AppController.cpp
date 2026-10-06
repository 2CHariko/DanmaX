#include "application/AppController.h"
#include "platform/windows/PlatformInfo.h"
#include <utility>

AppController::AppController(QString dataDirectory, QObject* parent)
    : QObject(parent), dataDirectory_(std::move(dataDirectory)) {}
QObject* AppController::preview() { return &preview_; }
bool AppController::overlayVisible() const { return overlayVisible_; }
void AppController::setOverlayVisible(bool visible) {
    if (overlayVisible_ == visible) return;
    overlayVisible_ = visible;
    if (visible) preview_.reset();
    preview_.setRunning(visible);
    emit overlayVisibleChanged();
}
QString AppController::dataDirectory() const { return dataDirectory_; }
QString AppController::platformDescription() const { return windowsPlatformDescription(); }
