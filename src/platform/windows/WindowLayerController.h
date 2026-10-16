#pragma once
#include <QObject>
#include <QPointer>
#include <QWindow>
#include <QTimer>
#include <functional>
#include <vector>

// Owns native Z-order policy without changing Qt flags or activating a window.
class WindowLayerController final : public QObject {
    Q_OBJECT
public:
    using Positioner = std::function<bool(quintptr, quintptr, unsigned)>;
    explicit WindowLayerController(QObject* parent = nullptr, Positioner positioner = {});
    ~WindowLayerController() override;
    void attach(QWindow* main, QWindow* overlay, QWindow* toolbar);
    void invalidate();
    bool reconcile(bool onTop, bool panelForeground, bool overlayAllowed, bool checkOrder = false);
signals:
    void invalidated();
    void operationFailed(const QString& message);
    void orderAdjusted();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    quintptr handle(QWindow* window) const;
    bool topmost(QWindow* window) const;
    bool position(quintptr window, quintptr after);
    bool restoreMain();
    void scheduleInvalidation();
    void updateMaintenance();
    void stopMaintenance();
    void maintain();
    bool needsRepair(bool panel) const;
    QPointer<QWindow> main_, overlay_, toolbar_;
    Positioner positioner_;
    bool injected_{};
    quintptr baselineHandle_{};
    quintptr foregroundHandle_{};
    bool baselineTopmost_{}, modified_{}, dirty_{true}, reconciling_{}, queued_{};
    bool panelForeground_{}, overlayAllowed_{}, ready_{true}, failureReported_{};
    bool onTop_{};
    QTimer maintenanceTimer_, eventTimer_;
    std::vector<quintptr> eventHooks_;
};
