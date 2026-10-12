#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QWindow>

class ToolbarInputFilter final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit ToolbarInputFilter(QObject* parent = nullptr);
    ~ToolbarInputFilter() override;

    void attach(QWindow* window);
    QWindow* window() const { return window_; }

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyWindowStyles();
    QPointer<QWindow> window_;
    quintptr nativeId_{};
};
