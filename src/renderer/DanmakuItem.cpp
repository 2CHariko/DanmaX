#include "renderer/DanmakuItem.h"
#include <QFontMetricsF>
#include <QMatrix4x4>
#include <QQuickWindow>
#include <QSGTextNode>
#include <QVariantMap>
#include <unordered_map>

namespace {
struct Entry {
    QSGTransformNode* node{};
    std::shared_ptr<QTextLayout> layout;
    quint64 frame{};
};
class Root final : public QSGNode {
  public:
    quint64 generation{};
    qreal dpr{};
    quint64 frame{};
    std::unordered_map<quint64, Entry> entries;
};
} // namespace
DanmakuItem::DanmakuItem(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    setClip(true);
}
void DanmakuItem::configure(const QVariantMap& s) {
    QFont font(s["fontFamily"].toString());
    font.setPixelSize(s["fontSize"].toInt());
    font.setWeight(QFont::DemiBold);
    const int stroke = s["strokeWidth"].toInt();
    if (font_ != font || stroke_ != stroke) {
        font_ = font;
        stroke_ = stroke;
        layouts_.clear();
        visuals_.clear();
        ++generation_;
    }
    spacing_ = s["lineSpacing"].toDouble();
    setOpacity(s["opacity"].toDouble());
    update();
}
DanmakuItem::Layout* DanmakuItem::layout(const std::string& raw) {
    const auto key = QString::fromUtf8(raw.data(), static_cast<qsizetype>(raw.size()));
    if (auto* found = layouts_.object(key)) {
        ++hits_;
        return found;
    }
    ++misses_;
    auto text = std::make_shared<QTextLayout>(key, font_);
    text->setCacheEnabled(true);
    text->beginLayout();
    auto line = text->createLine();
    line.setLineWidth(100000);
    text->endLayout();
    auto* entry = new Layout{text, line.naturalTextWidth() + stroke_ * 2};
    layouts_.insert(key, entry, static_cast<int>(1 + (key.size() * 64 + 1023) / 1024));
    return entry;
}
double DanmakuItem::measure(const danmaku::Item& item) {
    return layout(item.text)->width;
}
double DanmakuItem::trackHeight() const {
    return QFontMetricsF(font_).height() * (1 + spacing_) + stroke_ * 2;
}
void DanmakuItem::present(const danmaku::Engine& engine) {
    const auto& active = engine.activeSlots();
    visuals_.resize(active.size());
    for (std::size_t i = 0; i < active.size(); ++i) {
        const auto& a = active[i];
        auto& visual = visuals_[i];
        if (!a.alive) {
            visual = {};
            continue;
        }
        if (visual.id != a.id) {
            const auto& data = engine.items()[a.item];
            visual = {a.id, a.x, a.y, 0xff000000 | data.color, layout(data.text)->text};
        } else {
            visual.x = a.x;
            visual.y = a.y;
        }
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
    for (const auto& visual : visuals_) {
        if (!visual.id)
            continue;
        auto it = root->entries.find(visual.id);
        if (it == root->entries.end()) {
            auto* group = new QSGTransformNode;
            root->appendChildNode(group);
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
            it = root->entries.emplace(visual.id, Entry{group, visual.layout}).first;
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
    return root;
}
