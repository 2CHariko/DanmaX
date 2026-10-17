#pragma once
#include "core/DanmakuEngine.h"
#include "application/DanmakuLibrary.h"
#include <QObject>
#include <QVariantMap>
#include <thread>
class AppController;

// Standalone product task and retained C++ renderer/clock fixture state.
class DanmakuSession final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* library READ library CONSTANT)
    Q_PROPERTY(bool active READ active NOTIFY stateChanged)
    Q_PROPERTY(bool canSelectSource READ active NOTIFY stateChanged)
    Q_PROPERTY(quint64 taskId READ taskId NOTIFY stateChanged)
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
    ~DanmakuSession() override;
    QObject* library() { return &library_; }
    bool active() const;
    quint64 taskId() const { return task_; }
    bool preparing() const { return preparing_; }
    void openFile(const QString& file);
    Q_INVOKABLE void loadFile(const QString& file);
    Q_INVOKABLE void loadCached(const QString& key);
    Q_INVOKABLE void searchDanmaku();
    void searchForAnime(const QString& title, const QString& episodeTitle = {}, double episodeNumber = 0);
    void configure(const QStringList& servers);
    void deactivate();
    void cancelPreparation();
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
    void start(bool manual = false);
    void stop();
    void unload();
    void togglePause();
    void seek(double position);
  signals:
    void changed();
    void stateChanged();
    void sourceCommitted();
    void sourceFailed(QString error);
    void requestOnlineDanmakuSearch();
  private:
    friend class AppController;
    friend class NowPlayingModel;
    AppController* app_;
    void prepareXml(const QString& file, bool opening);
    void cancelXml();
    void accept(std::vector<danmaku::Item> items, const QString& title, const QVariantMap& metadata, const QString& file, bool opening);
    DanmakuLibrary library_;
    std::jthread loader_;
    quint64 task_{}, request_{}, libraryTask_{};
    bool productActive_{}, preparing_{};
    std::vector<danmaku::Item> items_;
    QVariantMap metadata_;
    QString title_, file_, error_, status_{QStringLiteral("请选择弹幕来源")};
    bool manual_{};
};
