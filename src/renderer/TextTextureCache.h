#pragma once
#include <QHash>
#include <QPainter>
#include <QQuickWindow>
#include <QSGTexture>
#include <QTextLayout>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <cmath>
#include <memory>
#include "renderer/TextureBudget.h"

// Render-thread-only resources. Callers destroy borrowing scene nodes before this cache.
class TextTextureCache {
  public:
    struct Resource {
        std::unique_ptr<QSGTexture> texture;
        QSizeF size;
        qint64 bytes{};
    };
    static constexpr qint64 maxUploadBytes = 4 * 1024 * 1024;
    enum class Reason { None, Capacity, Preparation, Dimensions, SingleUpload, Allocation };
    struct Description {
        QString key;
        QSize pixels;
        qint64 bytes{};
        Reason ineligible{Reason::None};
    };
    struct Result {
        std::shared_ptr<Resource> texture;
        Reason reason{Reason::None};
    };
    static Description describe(const QTextLayout& layout, QRgb color, int stroke, qreal dpr) {
        Description result;
        result.key = layout.font().toString() + QChar(0) + layout.text() + QChar(0) +
                     QString::number(color) + QChar(0) + QString::number(stroke) + QChar(0) + QString::number(dpr);
        const double width = layout.lineCount() ? layout.lineAt(0).naturalTextWidth() : 0;
        result.pixels = QSize(static_cast<int>(std::ceil((width + stroke * 2 + 2) * dpr)),
                             static_cast<int>(std::ceil((layout.boundingRect().height() + stroke * 2 + 2) * dpr)));
        result.bytes = static_cast<qint64>(result.pixels.width()) * result.pixels.height() * 4;
        if (result.pixels.width() <= 0 || result.pixels.height() <= 0 ||
            result.pixels.width() > 8192 || result.pixels.height() > 8192)
            result.ineligible = Reason::Dimensions;
        else if (result.bytes > maxUploadBytes) result.ineligible = Reason::SingleUpload;
        return result;
    }
    void setBudget(qint64 bytes) { budget_ = bytes; }
    void beginFrame() {
        uploaded_ = 0;
        preparationNs_ = 0;
    }
    Result acquire(QQuickWindow* window, const QTextLayout& layout, const Description& description,
                   QRgb color, int stroke, qreal dpr) {
        if (auto found = entries_.value(description.key)) return {found};
        if (description.ineligible != Reason::None) return {{}, description.ineligible};
        const auto bytes = description.bytes;
        if (bytes > budget_ - bytes_) return {{}, Reason::Capacity};
        if (bytes > maxUploadBytes - uploaded_ || preparationNs_ >= 2 * 1000 * 1000)
            return {{}, Reason::Preparation};
        // Charge only raster/texture preparation. Counting unrelated scene updates
        // here would starve later comments on slower renderers or Debug builds.
        QElapsedTimer preparation;
        preparation.start();
        const auto account = qScopeGuard([&] { preparationNs_ += preparation.nsecsElapsed(); });
        QImage image(description.pixels, QImage::Format_ARGB32_Premultiplied);
        if (image.isNull()) return {{}, Reason::Allocation};
        image.setDevicePixelRatio(dpr);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setPen(QColor::fromRgba(color));
        layout.draw(&painter, QPointF(stroke, stroke));
        painter.end();
        if (stroke > 0) {
            // Rasterize glyphs once. Tint their alpha mask so color emoji also
            // receive a black outline instead of eight displaced color copies.
            const QImage foreground = image;
            QImage outline = image.copy();
            QPainter tint(&outline);
            tint.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tint.fillRect(QRectF(QPointF(), outline.deviceIndependentSize()), Qt::black);
            tint.end();
            image.fill(Qt::transparent);
            QPainter composite(&image);
            composite.setRenderHint(QPainter::SmoothPixmapTransform);
            for (const auto& p : {QPointF(-stroke, 0), QPointF(stroke, 0), QPointF(0, -stroke),
                                 QPointF(0, stroke), QPointF(-stroke * .7, -stroke * .7),
                                 QPointF(stroke * .7, -stroke * .7), QPointF(-stroke * .7, stroke * .7),
                                 QPointF(stroke * .7, stroke * .7)})
                composite.drawImage(p, outline);
            composite.drawImage(QPointF(), foreground);
        }
        auto* texture = window->createTextureFromImage(image, QQuickWindow::TextureCanUseAtlas);
        if (!texture) return {{}, Reason::Allocation};
        auto result = std::make_shared<Resource>();
        result->texture.reset(texture);
        result->size = QSizeF(description.pixels) / dpr;
        result->bytes = bytes;
        entries_.insert(description.key, result);
        bytes_ += bytes;
        uploaded_ += bytes;
        return {result};
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
    qint64 budget_{64 * TextureBudget::MiB};
    qint64 preparationNs_{};
};
