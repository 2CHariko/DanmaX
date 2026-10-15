#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <functional>

class SystemUiPreferences final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
    Q_PROPERTY(bool animationsEnabled READ animationsEnabled NOTIFY changed)
public:
    explicit SystemUiPreferences(QObject* parent = nullptr, std::function<bool()> query = {});
    ~SystemUiPreferences() override;
    bool animationsEnabled() const { return animationsEnabled_; }
    bool nativeEventFilter(const QByteArray&, void*, qintptr*) override;
public slots:
    void refresh();
signals:
    void changed();
private:
    std::function<bool()> query_;
    bool animationsEnabled_{true};
};
