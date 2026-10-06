#pragma once
#include "renderer/PreviewDriver.h"
#include <QObject>
#include <QString>

class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* preview READ preview CONSTANT)
    Q_PROPERTY(bool overlayVisible READ overlayVisible WRITE setOverlayVisible NOTIFY overlayVisibleChanged)
    Q_PROPERTY(QString dataDirectory READ dataDirectory CONSTANT)
    Q_PROPERTY(QString platformDescription READ platformDescription CONSTANT)
public:
    explicit AppController(QString dataDirectory, QObject* parent = nullptr);
    QObject* preview();
    bool overlayVisible() const;
    void setOverlayVisible(bool visible);
    QString dataDirectory() const;
    QString platformDescription() const;
signals:
    void overlayVisibleChanged();
private:
    PreviewDriver preview_;
    QString dataDirectory_;
    bool overlayVisible_{};
};
