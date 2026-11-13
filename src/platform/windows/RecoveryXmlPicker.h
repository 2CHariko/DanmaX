#pragma once

#include <QObject>
#include <QWindow>
#include <memory>

// One native request at a time. Cancel invalidates its result immediately,
// while busy stays true until the native modal loop has returned.
class RecoveryXmlPicker : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    explicit RecoveryXmlPicker(QObject* parent = nullptr);
    ~RecoveryXmlPicker() override;
    bool busy() const { return bool(request_); }
    Q_INVOKABLE void open(QWindow* owner);
    Q_INVOKABLE void cancel();
signals:
    void busyChanged();
    void accepted(const QString& file);
    void failed(const QString& message);
private:
    struct Request;
    std::shared_ptr<Request> request_;
    QMetaObject::Connection ownerDestroyed_;
};
