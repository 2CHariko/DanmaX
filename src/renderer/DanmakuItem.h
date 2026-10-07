#pragma once
#include "core/DanmakuEngine.h"
#include <QCache>
#include <QQuickItem>
#include <QTextLayout>
#include <memory>
#include <atomic>
class DanmakuItem : public QQuickItem {
    Q_OBJECT
  public:
    explicit DanmakuItem(QQuickItem* parent = nullptr);
    bool configure(const QVariantMap& settings); // Whether live geometry needs remeasurement.
    void present(const danmaku::Engine& engine);
    danmaku::Engine::Extent measure(const danmaku::Item& item);
    double trackHeight() const;
    int snapshotCount() const { return static_cast<int>(visuals_.size()); }
    quint64 firstSnapshotId() const { return visuals_.empty() ? 0 : visuals_.front().id; }
    double firstSnapshotX() const { return visuals_.empty() ? 0 : visuals_.front().x; }
    double firstSnapshotFontSize() const { return visuals_.empty() ? 0 : visuals_.front().layout->font().pixelSize(); }
    bool imageBackend() const { return imageBackend_; }
    int imageNodeCount() const { return imageNodes_.load(); }
    qint64 textureBytes() const { return textureBytes_.load(); }
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
    const bool imageBackend_{qEnvironmentVariable("DANMAKU_RENDER_BACKEND") == "image"};
    std::atomic<int> imageNodes_{};
    std::atomic<qint64> textureBytes_{};
    double spacing_{0.2};
    quint64 generation_{}, hits_{}, misses_{};
    // Estimated layout budget in KiB; active scene nodes additionally bounded by maxActive.
    QCache<QString, Layout> layouts_{16384};
    std::vector<Visual> visuals_;
    std::vector<Visual> slotVisuals_;
    std::vector<std::size_t> previousSlots_;
};
