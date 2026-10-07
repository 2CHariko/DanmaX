#pragma once
#include "core/DanmakuEngine.h"
#include "core/MediaClock.h"
#include "infrastructure/LogModel.h"
#include "infrastructure/SettingsStore.h"
#include "platform/windows/MediaMonitor.h"
#include "renderer/DanmakuItem.h"
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QWindow>
#include <thread>
#include <functional>
#include <memory>
struct XmlResult;
class AppController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject* settings READ settings CONSTANT)
    Q_PROPERTY(QObject* logs READ logs CONSTANT)
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QStringList screens READ screens NOTIFY screensChanged)
    Q_PROPERTY(QString status READ status NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY stateChanged)
    Q_PROPERTY(QString mediaTitle READ mediaTitle NOTIFY stateChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
    Q_PROPERTY(int loadProgress READ loadProgress NOTIFY stateChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY stateChanged)
    Q_PROPERTY(bool manualMode READ manualMode NOTIFY stateChanged)
    Q_PROPERTY(bool overlayVisible READ overlayVisible NOTIFY stateChanged)
    Q_PROPERTY(int total READ total NOTIFY stateChanged)
    Q_PROPERTY(double position READ position NOTIFY metricsChanged)
    Q_PROPERTY(double duration READ duration NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap metrics READ metrics NOTIFY metricsChanged)
    Q_PROPERTY(bool hasFluentIcons READ hasFluentIcons CONSTANT)
  public:
    using ForegroundQuery = std::function<bool(const QString&)>;
    explicit AppController(QString dataDirectory, QObject* parent = nullptr,
                           ForegroundQuery foregroundQuery = {});
    ~AppController() override;
    QObject* settings() {
        return &settings_;
    }
    QObject* logs() {
        return &logs_;
    }
    QVariantList sessions() const {
        return sessions_;
    }
    QStringList screens() const;
    QString status() const {
        return status_;
    }
    QString error() const {
        return error_;
    }
    QString filePath() const {
        return file_;
    }
    QString mediaTitle() const {
        return mediaTitle_;
    }
    bool loading() const {
        return loading_;
    }
    int loadProgress() const {
        return progress_;
    }
    bool running() const {
        return running_;
    }
    bool playing() const {
        return playing_;
    }
    bool manualMode() const {
        return manual_;
    }
    bool overlayVisible() const {
        return visible_;
    }
    int total() const {
        return static_cast<int>(engine_.items().size());
    }
    double position() const {
        return position_;
    }
    double duration() const {
        return duration_;
    }
    QVariantMap metrics() const {
        return metrics_;
    }
    bool hasFluentIcons() const;
    Q_INVOKABLE void attach(QObject* renderer, QObject* overlay);
    Q_INVOKABLE void loadFile(const QString& path);
    Q_INVOKABLE void cancelLoad();
    Q_INVOKABLE void start(bool manual = false);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void togglePause();
    Q_INVOKABLE void seek(double position);
    Q_INVOKABLE void selectSession(const QString& id);
    Q_INVOKABLE void exportLogs(const QString& path);
    Q_INVOKABLE void clearError();
    void beginDemo(int activeCount = 40);
    void writeReport(const QString& path);
  signals:
    void stateChanged();
    void metricsChanged();
    void sessionsChanged();
    void screensChanged();
    void loadCompleted(bool success);

  private slots:
    void onSample(const MediaSample& sample);

  private:
    void tick();
    void configure();
    void applyRendererSettings();
    void discardLoad();

    void fail(const QString& message);
    void updateWindow();
    void updateMetrics();
    static QString localPath(const QString& path);
    SettingsStore settings_;
    LogModel logs_;
    MediaMonitor monitor_;
    ForegroundQuery foregroundQuery_;
    danmaku::Engine engine_;
    danmaku::MediaClock mediaClock_;
    QElapsedTimer monotonicTime_;
    QPointer<DanmakuItem> renderer_;
    QPointer<QWindow> overlay_;
    QTimer timer_, metricsTimer_;
    QElapsedTimer frameTime_, sampleTime_, foregroundTime_, metricsTime_;
    std::jthread loader_;
    std::shared_ptr<XmlResult> pendingLoad_;
    quint64 loadGeneration_{};
    QVariantList sessions_;
    QVariantMap metrics_, lastSettings_, appliedRendererSettings_;
    bool rendererSettingsPending_{};
    QString status_{QStringLiteral("请选择弹幕文件")}, error_, file_, mediaTitle_, mediaIdentity_;
    bool foreground_{true};
    bool loading_{}, running_{}, playing_{}, manual_{}, visible_{}, snapshotDirty_{true}, demo_{};
    int progress_{}, frames_{};
    double position_{}, duration_{}, rate_{1}, samplePosition_{};
    std::vector<double> frameTimes_;
    double engineMs_{}, snapshotMs_{};
    quint64 renderedFrames_{};
    quint64 lastMetricsRenderedFrames_{};
    quint64 animationCallbacks_{}, mediaSamples_{}, sampleResets_{}, frameTicks_{}, maintenanceTicks_{};
};
