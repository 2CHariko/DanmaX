#pragma once
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <functional>

// GUI-thread coordinate sensing also works when the toolbar HWND is hidden.
class ToolbarPointerMonitor final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool hovered READ hovered NOTIFY hoveredChanged)
  public:
    using CursorQuery = std::function<QPoint()>;
    explicit ToolbarPointerMonitor(QObject* parent = nullptr, CursorQuery cursorQuery = {});
    void attach(QWindow* overlay, QWindow* toolbar);
    void setEnabled(bool enabled);
    bool enabled() const { return timer_.isActive(); }
    bool hovered() const { return hovered_; }
    QRect activationRect() const;
  signals:
    void enabledChanged();
    void hoveredChanged();
  private:
    void sample();
    void setHovered(bool hovered);
    CursorQuery cursorQuery_;
    QPointer<QWindow> overlay_, toolbar_;
    QTimer timer_;
    bool hovered_{};
};
