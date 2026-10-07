#include "renderer/DanmakuItem.h"
#include "renderer/TextTextureCache.h"
#include <QSGSimpleTextureNode>
#include <QFontMetricsF>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGTextNode>
#include <QVariantMap>
#include <algorithm>
#include <unordered_map>

namespace {
struct Entry {
    QSGTransformNode* node{};
    std::shared_ptr<QTextLayout> layout;
    quint64 frame{};
    std::shared_ptr<TextTextureCache::Resource> texture;
};
class Root final : public QSGNode {
  public:
    quint64 generation{};
    qreal dpr{};
    quint64 frame{};
    std::unordered_map<quint64, Entry> entries;
    TextTextureCache textures;
    ~Root() override {
        while (firstChild()) {
            auto* child = firstChild();
            removeChildNode(child);
            delete child;
        }
        entries.clear();
    }
};
} // namespace
DanmakuItem::DanmakuItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setClip(true);
}
bool DanmakuItem::configure(const QVariantMap& s) {
    QFont font(s["fontFamily"].toString());
    font.setPixelSize(s["fontSize"].toInt());
    font.setWeight(QFont::DemiBold);
    const int stroke = s["strokeWidth"].toInt();
    const bool dimensionsChanged = font_ != font || stroke_ != stroke || spacing_ != s["lineSpacing"].toDouble();
    if (font_ != font) layouts_.clear();
    if (font_ != font || stroke_ != stroke) {
        font_ = font;
        stroke_ = stroke;
        // Keep the current snapshot alive until present() atomically replaces it.
        slotVisuals_.clear();
        previousSlots_.clear();
        ++generation_;
    }
    spacing_ = s["lineSpacing"].toDouble();
    setOpacity(s["opacity"].toDouble());
    update();
    return dimensionsChanged;
}
DanmakuItem::Layout* DanmakuItem::layout(const danmaku::Item& item) {
    QFont font = font_;
    font.setPixelSize(std::max(1, qRound(font_.pixelSize() * std::clamp(item.fontSize, 1, 200) / 25.0)));
    const auto content = QString::fromUtf8(item.text.data(), static_cast<qsizetype>(item.text.size()));
    const auto key = QString::number(font.pixelSize()) + QChar(0) + content;
    if (auto* found = layouts_.object(key)) {
        ++hits_;
        return found;
    }
    ++misses_;
    auto text = std::make_shared<QTextLayout>(content, font);
    text->setCacheEnabled(true);
    text->beginLayout();
    auto line = text->createLine();
    line.setLineWidth(100000);
    text->endLayout();
    auto* entry = new Layout{text};
    layouts_.insert(key, entry, static_cast<int>(1 + (key.size() * 64 + 1023) / 1024));
    return entry;
}
danmaku::Engine::Extent DanmakuItem::measure(const danmaku::Item& item) {
    const auto* entry = layout(item);
    return {entry->text->lineAt(0).naturalTextWidth() + stroke_ * 2,
            QFontMetricsF(entry->text->font()).height() * (1 + spacing_) + stroke_ * 2};
}
double DanmakuItem::trackHeight() const {
    return QFontMetricsF(font_).height() * (1 + spacing_) + stroke_ * 2;
}
void DanmakuItem::present(const danmaku::Engine& engine) {
    const auto& active = engine.activeSlots();
    // Retire references before reusing slots; only live slots enter the scene snapshot.
    for (auto slot : previousSlots_)
        if (slot < slotVisuals_.size() && (slot >= active.size() || !active[slot].alive))
            slotVisuals_[slot] = {};
    slotVisuals_.resize(active.size());
    previousSlots_ = engine.activeIndices();
    visuals_.clear();
    visuals_.reserve(engine.activeCount());
    for (const auto i : engine.activeIndices()) {
        const auto& a = active[i];
        auto& visual = slotVisuals_[i];
        if (!a.alive) {
            visual = {};
            continue;
        }
        if (visual.id != a.id) {
            const auto& data = engine.items()[a.item];
            visual = {a.id, a.x, a.y, 0xff000000 | data.color, layout(data)->text};
        } else {
            visual.x = a.x;
            visual.y = a.y;
        }
        visuals_.push_back(visual);
    }
    update();
}
QSGNode* DanmakuItem::updatePaintNode(QSGNode* old, UpdatePaintNodeData*) {
    auto* root = static_cast<Root*>(old);
    const auto dpr = window()->effectiveDevicePixelRatio();
    if (root && (root->generation != generation_ || root->dpr != dpr)) {
        delete root;
        root = nullptr;
    }
    if (!root) {
        root = new Root;
        root->generation = generation_;
        root->dpr = dpr;
    }
    ++root->frame;
    root->textures.beginFrame();
    for (const auto& visual : visuals_) {
        if (!visual.id)
            continue;
        auto it = root->entries.find(visual.id);
        if (it == root->entries.end()) {
            auto* group = new QSGTransformNode;
            root->appendChildNode(group);
            auto raster = imageBackend_ ? root->textures.acquire(window(), *visual.layout, visual.color, stroke_, dpr)
                                        : std::shared_ptr<TextTextureCache::Resource>{};
            if (raster) {
                auto* imageNode = new QSGSimpleTextureNode;
                imageNode->setTexture(raster->texture.get());
                imageNode->setRect(QRectF(QPointF(), raster->size));
                imageNode->setFiltering(QSGTexture::Linear);
                group->appendChildNode(imageNode);
            } else {
                auto add = [&](QPointF offset, QColor color) {
                    auto* text = window()->createTextNode();
                    text->setColor(color);
                    text->setRenderType(QSGTextNode::QtRendering);
                    text->addTextLayout(offset, visual.layout.get());
                    group->appendChildNode(text);
                };
                // Public scene-graph nodes, cached for the comment lifetime. No per-frame glyph layout.
                if (stroke_ > 1) {
                    for (const auto& p :
                         {QPointF(-stroke_, 0), QPointF(stroke_, 0), QPointF(0, -stroke_), QPointF(0, stroke_),
                          QPointF(-stroke_ * 0.7, -stroke_ * 0.7), QPointF(stroke_ * 0.7, -stroke_ * 0.7),
                          QPointF(-stroke_ * 0.7, stroke_ * 0.7), QPointF(stroke_ * 0.7, stroke_ * 0.7)})
                        add(p + QPointF(stroke_, stroke_), Qt::black);
                }
                if (stroke_ == 1) {
                    auto* text = window()->createTextNode();
                    text->setColor(QColor::fromRgba(visual.color));
                    text->setTextStyle(QSGTextNode::Outline);
                    text->setStyleColor(Qt::black);
                    text->addTextLayout(QPointF(1, 1), visual.layout.get());
                    group->appendChildNode(text);
                } else
                    add(QPointF(stroke_, stroke_), QColor::fromRgba(visual.color));
            }
            it = root->entries.emplace(visual.id, Entry{group, visual.layout, 0, raster}).first;
        }
        it->second.frame = root->frame;
        QMatrix4x4 matrix;
        matrix.translate(static_cast<float>(visual.x), static_cast<float>(visual.y));
        it->second.node->setMatrix(matrix);
    }
    for (auto it = root->entries.begin(); it != root->entries.end();) {
        if (it->second.frame != root->frame) {
            root->removeChildNode(it->second.node);
            delete it->second.node;
            it = root->entries.erase(it);
        } else
            ++it;
    }
    root->textures.retireUnused();
    int imageCount = 0;
    for (const auto& entry : root->entries) if (entry.second.texture) ++imageCount;
    imageNodes_.store(imageCount);
    textureBytes_.store(root->textures.bytes());
    return root;
}
