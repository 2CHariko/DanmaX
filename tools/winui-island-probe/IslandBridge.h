#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QString>
#include <memory>

class QQuickWindow;
class QQuickItem;
class IslandBridge final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(int selectedIndex READ selectedIndex NOTIFY selectionChanged)
    Q_PROPERTY(QString selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit IslandBridge(QObject* parent = nullptr);
    ~IslandBridge() override;
    void attach(QQuickWindow* window, QQuickItem* slot);
    void close();
    int selectedIndex() const;
    QString selection() const;
    QString status() const;
    Q_INVOKABLE void syncGeometry();
    Q_INVOKABLE void setDark(bool value);
    Q_INVOKABLE void focusNative(bool reverse = false);
    Q_INVOKABLE void focusQt();
    Q_INVOKABLE void selectLast();
    Q_INVOKABLE void recreate();
    Q_INVOKABLE void setPopupOpen(bool value);
    Q_INVOKABLE void report(const QString& event);
    bool smokeCheck();
    bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override;
Q_SIGNALS:
    void selectionChanged();
    void statusChanged();
    void leaveNative(bool reverse);
private:
    struct State;
    std::unique_ptr<State> s;
};
