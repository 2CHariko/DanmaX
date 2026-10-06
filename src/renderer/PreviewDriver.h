#pragma once
#include "core/PlaybackClock.h"
#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class PreviewDriver final : public QObject {
    Q_OBJECT
    Q_PROPERTY(double elapsedSeconds READ elapsedSeconds NOTIFY frameChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
public:
    explicit PreviewDriver(QObject* parent = nullptr);
    [[nodiscard]] double elapsedSeconds() const;
    [[nodiscard]] bool running() const;
    Q_INVOKABLE void setRunning(bool running);
    Q_INVOKABLE void reset();
signals:
    void frameChanged();
    void runningChanged();
private:
    void tick();
    danmaku::PlaybackClock clock_;
    QElapsedTimer elapsed_;
    QTimer timer_;
};
