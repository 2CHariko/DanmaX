#pragma once
#include "core/DanmakuEngine.h"
#include <QCache>
#include <QQuickItem>
#include <QTextLayout>
#include <memory>
#include <atomic>
struct DanmakuSceneState;
class DanmakuItem : public QQuickItem {
    Q_OBJECT
  public:
    explicit DanmakuItem(QQuickItem* parent = nullptr);
    bool configure(const QVariantMap& settings); // Whether live geometry needs remeasurement.
    void present(const danmaku::Engine& engine);
    void clearContent(); // GUI state only; the window releases scene-graph resources.
    danmaku::Engine::Extent measure(const danmaku::Item& item);
    double trackHeight() const;
    int snapshotCount() const { return static_cast<int>(visuals_.size()); }
    quint64 firstSnapshotId() const { return visuals_.empty() ? 0 : visuals_.front().id; }
    double firstSnapshotX() const { return visuals_.empty() ? 0 : visuals_.front().x; }
    double firstSnapshotFontSize() const { return visuals_.empty() ? 0 : visuals_.front().layout->font().pixelSize(); }
    bool imageBackend() const { return imageBackend_; }
    int imageNodeCount() const { return imageNodes_.load(); }
    qint64 textureBytes() const { return textureBytes_.load(); }
    int sceneEntries() const { return sceneEntries_.load(); }
    qint64 textureBudgetBytes() const { return textureBudgetBytes_.load(); }
    qint64 textureDemandBytes() const { return textureDemandBytes_.load(); }
    QVariantMap textureFallbackReasons() const;
    int cacheEntries() const {
        return layouts_.size();
    }
    quint64 cacheHits() const {
        return hits_;
    }
    quint64 cacheMisses() const {
        return misses_;
    }

  protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData*) override;

  private:
    struct Layout {
        std::shared_ptr<QTextLayout> text;
    };
    struct Visual {
        quint64 id{};
        double x{}, y{};
        QRgb color{};
        std::shared_ptr<QTextLayout> layout;
    };
    Layout* layout(const danmaku::Item& item);
    QFont font_;
    int stroke_{1};
    bool textureBudgetAuto_{true};
    int textureBudgetLimitMiB_{512};
    // Cached whole-comment textures avoid separate outline/fill glyph batches.
    // Keep the public Qt text path available for comparison and budget fallbacks.
    const bool imageBackend_{qEnvironmentVariable("DANMAKU_RENDER_BACKEND") != "text"};
    std::atomic<int> imageNodes_{};
    std::atomic<qint64> textureBytes_{};
    std::atomic<int> sceneEntries_{};
    std::atomic<qint64> textureBudgetBytes_{}, textureDemandBytes_{};
    std::atomic<int> capacityFallbacks_{}, preparationFallbacks_{}, sizeFallbacks_{}, allocationFallbacks_{};
    QMetaObject::Connection invalidationConnection_;
    QMetaObject::Connection stoppingConnection_;
    // Accessed only during render-thread signals/synchronization, never on the GUI thread.
    std::weak_ptr<DanmakuSceneState> renderScene_;
    double spacing_{0.2};
    quint64 generation_{}, hits_{}, misses_{};
    // Estimated layout budget in KiB; active scene nodes additionally bounded by maxActive.
    QCache<QString, Layout> layouts_{16384};
    std::vector<Visual> visuals_;
    std::vector<Visual> slotVisuals_;
    std::vector<std::size_t> previousSlots_;
};
