#pragma once
#include <QObject>
#include <QPointer>
#include <QQuickItem>
#include <QWindow>
#include <memory>
class QQuickWindow;

class VideoViewportController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QWindow* videoWindow READ videoWindow CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool shown READ shown NOTIFY changed)
    Q_PROPERTY(QString releaseStatus READ releaseStatus NOTIFY changed)
    Q_PROPERTY(bool dragging READ dragging NOTIFY changed)
    Q_PROPERTY(QString dragName READ dragName NOTIFY changed)
public:
    explicit VideoViewportController(QObject* parent = nullptr);
    ~VideoViewportController() override;
    QWindow* videoWindow() const { return video_.get(); }
    bool active() const { return active_; }
    bool shown() const { return shown_; }
    QString releaseStatus() const { return releaseStatus_; }
    bool dragging() const { return dragging_; }
    QString dragName() const { return dragName_; }
    quintptr nativeHandle();
    Q_INVOKABLE void attach(QQuickItem* item);
    void setActive(bool active);
    void setOverlay(QWindow* overlay);
    void setReleaseStatus(const QString& value) { releaseStatus_ = value; emit changed(); }
    void synchronize();
signals:
    void changed();
    void videoDropped(QString path);
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    void placeDialog(QQuickWindow* dialog);
    std::unique_ptr<QWindow> video_;
    QPointer<QQuickItem> item_;
    QPointer<QWindow> overlay_, host_;
    bool active_{}, shown_{};
    QString releaseStatus_;
    bool dragging_{};
    QString dragName_;
};
