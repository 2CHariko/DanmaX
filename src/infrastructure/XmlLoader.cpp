#include "infrastructure/XmlLoader.h"
#include <QFile>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace {
// Keep XML streaming and its declared encoding. Replace forbidden C0 characters
// with a space of the same byte width, preserving error line/column positions.
// Do not repair markup, entities, invalid UTF-8 or truncated documents.
class SanitizedXmlDevice final : public QIODevice {
  public:
    SanitizedXmlDevice(QFile& file, int& repaired, std::stop_token stop)
        : file_(file), repaired_(repaired), stop_(stop) {
        const auto prefix = file.peek(4);
        const auto byte = [&](int i) { return static_cast<unsigned char>(prefix[i]); };
        if (prefix.size() >= 4 &&
            ((byte(0) == 0xff && byte(1) == 0xfe && byte(2) == 0 && byte(3) == 0) ||
             (byte(0) == '<' && byte(1) == 0 && byte(2) == 0 && byte(3) == 0))) {
            width_ = 4; littleEndian_ = true;
        } else if (prefix.size() >= 4 &&
                   ((byte(0) == 0 && byte(1) == 0 && byte(2) == 0xfe && byte(3) == 0xff) ||
                    (byte(0) == 0 && byte(1) == 0 && byte(2) == 0 && byte(3) == '<'))) {
            width_ = 4; littleEndian_ = false;
        } else if (prefix.size() >= 2 &&
                   ((byte(0) == 0xff && byte(1) == 0xfe) || (byte(0) == '<' && byte(1) == 0))) {
            width_ = 2; littleEndian_ = true;
        } else if (prefix.size() >= 2 &&
                   ((byte(0) == 0xfe && byte(1) == 0xff) || (byte(0) == 0 && byte(1) == '<'))) {
            width_ = 2; littleEndian_ = false;
        }
        open(QIODevice::ReadOnly);
    }
    bool isSequential() const override { return true; }
    bool atEnd() const override {
        return QIODevice::bytesAvailable() == 0 && offset_ == pending_.size() && file_.atEnd();
    }
    qint64 bytesAvailable() const override {
        return QIODevice::bytesAvailable() + pending_.size() - offset_ + file_.bytesAvailable();
    }

  protected:
    qint64 readData(char* data, qint64 maxSize) override {
        if (maxSize <= 0 || stop_.stop_requested()) return 0;
        if (offset_ == pending_.size()) {
            pending_ = file_.read(64 * 1024);
            offset_ = 0;
            if (file_.error() != QFileDevice::NoError) {
                setErrorString(file_.errorString());
                return -1;
            }
            for (qsizetype i = 0; i + width_ <= pending_.size(); i += width_) {
                std::uint32_t code = 0;
                for (int j = 0; j < width_; ++j) {
                    const auto shift = 8 * (littleEndian_ ? j : width_ - j - 1);
                    code |= static_cast<std::uint32_t>(static_cast<unsigned char>(pending_[i + j])) << shift;
                }
                if (code < 0x20 && code != 9 && code != 10 && code != 13) {
                    for (int j = 0; j < width_; ++j)
                        pending_[i + j] = 0;
                    pending_[i + (littleEndian_ ? 0 : width_ - 1)] = ' ';
                    ++repaired_;
                }
            }
        }
        const auto count = std::min<qint64>(maxSize, pending_.size() - offset_);
        if (count > 0) std::memcpy(data, pending_.constData() + offset_, static_cast<std::size_t>(count));
        offset_ += count;
        return count;
    }
    qint64 writeData(const char*, qint64) override { return -1; }

  private:
    QFile& file_;
    int& repaired_;
    std::stop_token stop_;
    QByteArray pending_;
    qsizetype offset_{};
    int width_{1};
    bool littleEndian_{true};
};
} // namespace

XmlResult readDanmakuXml(const QString& path, std::stop_token stop,
                         const std::function<void(int)>& progress) {
    XmlResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = file.errorString();
        return result;
    }
    if (file.size() > 512LL * 1024 * 1024) {
        result.error = QStringLiteral("XML 超过 512 MiB 限制");
        return result;
    }
    SanitizedXmlDevice input(file, result.sanitizedCharacters, stop);
    QXmlStreamReader xml(&input);
    int count = 0, lastProgress = -1;
    while (!xml.atEnd()) {
        if (stop.stop_requested()) {
            result.cancelled = true;
            result.items.clear();
            return result;
        }
        xml.readNext();
        if (xml.isDTD()) {
            result.error = QStringLiteral("不支持包含 DTD 的 XML");
            result.items.clear();
            return result;
        }
        if (!xml.isStartElement() || xml.name() != QStringLiteral("d"))
            continue;
        if (++count > 1000000) {
            result.error = QStringLiteral("弹幕条数超过 100 万限制");
            result.items.clear();
            return result;
        }
        const auto fields = xml.attributes().value("p").toString().split(',');
        auto text = xml.readElementText(QXmlStreamReader::ErrorOnUnexpectedElement).trimmed();
        if (stop.stop_requested()) {
            result.cancelled = true;
            result.items.clear();
            return result;
        }
        const int percent = static_cast<int>(100 * file.pos() / std::max<qint64>(1, file.size()));
        if (progress && percent != lastProgress) {
            progress(percent);
            lastProgress = percent;
        }
        bool timeOk = false, modeOk = false, sizeOk = false, colorOk = false;
        const double time = fields.value(0).toDouble(&timeOk);
        const int mode = fields.value(1).toInt(&modeOk);
        const int fontSize = fields.value(2).toInt(&sizeOk);
        const auto colorField = fields.value(3).trimmed();
        const bool missingColor = fields.size() >= 4 &&
            (colorField.isEmpty() || colorField.compare("undefined", Qt::CaseInsensitive) == 0 ||
             colorField.compare("null", Qt::CaseInsensitive) == 0);
        auto color = colorField.toULongLong(&colorOk);
        if (missingColor) {
            color = 0xffffff;
            colorOk = true;
        }
        if (!timeOk || !std::isfinite(time) || time < 0 || time > 604800 || !modeOk ||
            !sizeOk || fontSize < 1 || fontSize > 200 ||
            !colorOk || color > 0xffffff || text.isEmpty() ||
            text.size() > 512) {
            ++result.skipped;
            ++result.invalidRecords;
            continue;
        }
        // Modes 2/3 are also ordinary scrolling comments. Reverse, positioned
        // and script modes need their own rendering semantics, not silent coercion.
        if (mode < 1 || mode > 5) {
            ++result.skipped;
            ++result.unsupportedModes;
            continue;
        }
        text.replace('\n', ' ');
        text.replace('\r', ' ');
        const auto mappedMode = mode <= 3 ? danmaku::Mode::Scroll : static_cast<danmaku::Mode>(mode);
        if (missingColor) ++result.defaultedColors;
        result.items.push_back({time, mappedMode, text.toUtf8().toStdString(),
                                static_cast<std::uint32_t>(color), fontSize});
    }
    if (stop.stop_requested()) {
        result.cancelled = true;
        result.items.clear();
        return result;
    }
    if (xml.hasError() || file.error() != QFileDevice::NoError) {
        result.items.clear();
        result.error = file.error() != QFileDevice::NoError ? file.errorString() :
            QStringLiteral("XML 第 %1 行，第 %2 列：%3")
                .arg(xml.lineNumber()).arg(xml.columnNumber()).arg(xml.errorString());
        return result;
    }
    if (progress)
        progress(-1); // Sorting has no honest byte-based progress.
    struct Cancelled {};
    std::size_t comparisons = 0;
    try {
        std::stable_sort(result.items.begin(), result.items.end(), [&](const auto& a, const auto& b) {
            if ((++comparisons & 4095) == 0 && stop.stop_requested())
                throw Cancelled{};
            return a.time < b.time;
        });
    } catch (const Cancelled&) {
        result.cancelled = true;
        result.items.clear();
    }
    if (stop.stop_requested()) {
        result.cancelled = true;
        result.items.clear();
    }
    return result;
}
