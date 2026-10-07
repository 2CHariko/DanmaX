#pragma once
#include <QHash>
#include <QPainter>
#include <QQuickWindow>
#include <QSGTexture>
#include <QTextLayout>
#include <cmath>
#include <memory>

// Render-thread-only resources. Callers destroy borrowing scene nodes before this cache.
class TextTextureCache {
  public:
    struct Resource {
        std::unique_ptr<QSGTexture> texture;
        QSizeF size;
        qint64 bytes{};
    };
    void beginFrame() { uploaded_ = 0; }
    std::shared_ptr<Resource> acquire(QQuickWindow* window, const QTextLayout& layout,
                                      QRgb color, int stroke, qreal dpr) {
        const QString key = layout.font().toString() + QChar(0) + layout.text() + QChar(0) + QString::number(color);
        if (auto found = entries_.value(key)) return found;
        const double textWidth = layout.lineCount() ? layout.lineAt(0).naturalTextWidth() : 0;
        const QSize pixels(static_cast<int>(std::ceil((textWidth + stroke * 2 + 2) * dpr)),
                           static_cast<int>(std::ceil((layout.boundingRect().height() + stroke * 2 + 2) * dpr)));
        const qint64 bytes = static_cast<qint64>(pixels.width()) * pixels.height() * 4;
        // Fall back to Qt text for this comment if preparation exceeds either budget.
        if (pixels.width() <= 0 || pixels.height() <= 0 || pixels.width() > 8192 || pixels.height() > 8192 ||
            bytes > 4 * 1024 * 1024 - uploaded_ || bytes > 64 * 1024 * 1024 - bytes_) return {};
        QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) return {};
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        if (stroke > 0) {
            painter.setPen(Qt::black);
            for (const auto& p : {QPointF(-stroke, 0), QPointF(stroke, 0), QPointF(0, -stroke),
                                 QPointF(0, stroke), QPointF(-stroke * .7, -stroke * .7),
                                 QPointF(stroke * .7, -stroke * .7), QPointF(-stroke * .7, stroke * .7),
                                 QPointF(stroke * .7, stroke * .7)})
                layout.draw(&painter, p + QPointF(stroke, stroke));
        }
        painter.setPen(QColor::fromRgba(color));
        layout.draw(&painter, QPointF(stroke, stroke));
        painter.end();
        auto* texture = window->createTextureFromImage(image);
        if (!texture) return {};
        auto result = std::make_shared<Resource>();
        result->texture.reset(texture);
        result->size = QSizeF(pixels) / dpr;
        result->bytes = bytes;
        entries_.insert(key, result);
        bytes_ += bytes;
        uploaded_ += bytes;
        return result;
    }
    void retireUnused() {
        for (auto it = entries_.begin(); it != entries_.end();) {
            if (it.value().use_count() == 1) {
                bytes_ -= it.value()->bytes;
                it = entries_.erase(it);
            } else ++it;
        }
    }
    qint64 bytes() const { return bytes_; }
  private:
    QHash<QString, std::shared_ptr<Resource>> entries_;
    qint64 bytes_{}, uploaded_{};
};
