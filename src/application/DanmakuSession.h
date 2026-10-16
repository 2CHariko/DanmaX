#pragma once
#include "application/AutomaticDanmaku.h"
#include "application/DanmakuLibrary.h"
#include "core/DanmakuEngine.h"
#include <QObject>
class AppController;

class DanmakuSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString sourceTitle READ sourceTitle NOTIFY changed)
    Q_PROPERTY(QString filePath READ filePath NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(int loadProgress READ loadProgress NOTIFY changed)
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(bool running READ running NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool manualMode READ manualMode NOTIFY changed)
    Q_PROPERTY(bool completed READ completed NOTIFY changed)
    Q_PROPERTY(double position READ position NOTIFY changed)
    Q_PROPERTY(double duration READ duration NOTIFY changed)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY changed)
    Q_PROPERTY(bool targetSessionConnected READ connected NOTIFY changed)
  public:
    DanmakuSession(const QString& data, const QString& cache, AppController* app);
    QString sourceTitle() const { return title_; }
    QString filePath() const { return file_; }
    int total() const { return static_cast<int>(items_.size()); }
    bool loading() const;
    int loadProgress() const;
    QString status() const;
    QString error() const { return error_; }
    bool running() const;
    bool playing() const;
    bool manualMode() const { return manual_; }
    bool completed() const;
    double position() const;
    double duration() const;
    QString mediaTitle() const;
    bool connected() const;
    Q_INVOKABLE void start(bool manual = false);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void unload();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double position);
  signals:
    void changed();
  private:
    friend class AppController;
    AppController* app_;
    DanmakuLibrary library_;
    AutomaticDanmaku automatic_;
    std::vector<danmaku::Item> items_;
    QString title_, file_, error_, status_{QStringLiteral("请选择弹幕来源")};
    bool manual_{};
};
