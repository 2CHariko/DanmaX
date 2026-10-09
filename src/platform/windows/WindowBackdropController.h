#pragma once
#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QPointer>
#include <QWindow>

class SettingsStore;
class LogModel;

// Owns only the control window's DWM backdrop; never touches the overlay.
class WindowBackdropController final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(bool effectiveMica READ effectiveMica NOTIFY changed)
    Q_PROPERTY(QString fallbackReason READ fallbackReason NOTIFY changed)
public:
    WindowBackdropController(SettingsStore* settings, LogModel* logs, QObject* parent = nullptr);
    ~WindowBackdropController() override;
    void attach(QWindow* window);
    bool effectiveMica() const { return effective_; }
    QString fallbackReason() const { return reason_; }
    static QString fallback(bool requested, bool nativeWindows, bool supported,
                            bool highContrast, bool transparent, bool savingPower);
signals:
    void changed();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override;
private:
    void refresh();
    void scheduleRefresh();
    void publish(bool enabled, const QString& reason);
    SettingsStore* settings_;
    LogModel* logs_;
    QPointer<QWindow> window_;
    bool surfaceReady_ = false;
    bool pending_ = false;
    bool effective_ = false;
    WId nativeId_ = 0;
    void* powerNotification_ = nullptr;
    QString reason_ = QStringLiteral("窗口尚未就绪");
};
