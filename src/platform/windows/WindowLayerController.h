#pragma once
#include <QObject>
#include <QPointer>
#include <QWindow>
#include <functional>

// Owns native Z-order policy without changing Qt flags or activating a window.
class WindowLayerController final : public QObject {
    Q_OBJECT
public:
    using Positioner = std::function<bool(quintptr, quintptr, unsigned)>;
    explicit WindowLayerController(QObject* parent = nullptr, Positioner positioner = {});
    ~WindowLayerController() override;
    void attach(QWindow* main, QWindow* overlay, QWindow* toolbar);
    void invalidate();
    bool reconcile(int strategy, bool panelForeground, bool overlayAllowed, bool periodic = false);
signals:
    void invalidated();
    void operationFailed(const QString& message);
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    quintptr handle(QWindow* window) const;
    bool topmost(QWindow* window) const;
    bool position(quintptr window, quintptr after);
    bool restoreMain();
    void scheduleInvalidation();
    QPointer<QWindow> main_, overlay_, toolbar_;
    Positioner positioner_;
    bool injected_{};
    quintptr baselineHandle_{};
    quintptr foregroundHandle_{};
    bool baselineTopmost_{}, modified_{}, dirty_{true}, reconciling_{}, queued_{};
    bool panelForeground_{}, overlayAllowed_{}, ready_{true}, failureReported_{};
    int strategy_{};
};
