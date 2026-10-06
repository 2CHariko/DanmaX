#pragma once
#include "core/DanmakuEngine.h"
#include <QCache>
#include <QQuickItem>
#include <QTextLayout>
#include <memory>
class DanmakuItem : public QQuickItem {
    Q_OBJECT
  public:
    explicit DanmakuItem(QQuickItem* parent = nullptr);
    void configure(const QVariantMap& settings);
    void present(const danmaku::Engine& engine);
    double measure(const danmaku::Item& item);
    double trackHeight() const;
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
        double width{};
    };
    struct Visual {
        quint64 id{};
        double x{}, y{};
        QRgb color{};
        std::shared_ptr<QTextLayout> layout;
    };
    Layout* layout(const std::string& text);
    QFont font_;
    int stroke_{1};
    double spacing_{0.2};
    quint64 generation_{}, hits_{}, misses_{};
    // Estimated layout budget in KiB; active scene nodes additionally bounded by maxActive.
    QCache<QString, Layout> layouts_{16384};
    std::vector<Visual> visuals_;
};
