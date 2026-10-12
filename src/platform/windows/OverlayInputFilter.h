#pragma once
#include <QAbstractNativeEventFilter>
#include <QList>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QWindow>

class OverlayInputFilter final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit OverlayInputFilter(QObject* parent = nullptr);
    ~OverlayInputFilter() override;

    void attach(QWindow* window);
    void setInteractiveRects(const QList<QRect>& rects);
    QList<QRect> interactiveRects() const { return interactiveRects_; }
    bool isHovering() const { return hovering_; }

signals:
    void hoverChanged(bool hovering);

public:
    // 以窗口本地逻辑像素坐标判断命中，方便测试与统一判定
    bool hitTest(const QPoint& localPos) const;

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyWindowStyles();
    QPointer<QWindow> window_;
    quintptr nativeId_{};
    QList<QRect> interactiveRects_;
    bool hovering_{false};
};
