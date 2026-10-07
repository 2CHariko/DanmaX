#include "infrastructure/XmlLoader.h"
#include <QFile>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
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
    QXmlStreamReader xml(&file);
    int count = 0, lastProgress = -1;
    while (!xml.atEnd()) {
        if (stop.stop_requested()) {
            result.cancelled = true;
            return result;
        }
        xml.readNext();
        if (xml.isDTD()) {
            result.error = QStringLiteral("不支持包含 DTD 的 XML");
            return result;
        }
        if (!xml.isStartElement() || xml.name() != QStringLiteral("d"))
            continue;
        const auto fields = xml.attributes().value("p").toString().split(',');
        auto text = xml.readElementText(QXmlStreamReader::ErrorOnUnexpectedElement).trimmed();
        bool timeOk = false, modeOk = false, sizeOk = false, colorOk = false;
        const double time = fields.value(0).toDouble(&timeOk);
        const int mode = fields.value(1).toInt(&modeOk);
        const int fontSize = fields.value(2).toInt(&sizeOk);
        const auto color = fields.value(3).toULongLong(&colorOk);
        if (!timeOk || !std::isfinite(time) || time < 0 || time > 604800 || !modeOk ||
            (mode != 1 && mode != 4 && mode != 5) || !sizeOk || fontSize < 1 || fontSize > 200 ||
            !colorOk || color > 0xffffff || text.isEmpty() ||
            text.size() > 512) {
            ++result.skipped;
            continue;
        }
        text.replace('\n', ' ');
        text.replace('\r', ' ');
        result.items.push_back({time, static_cast<danmaku::Mode>(mode), text.toUtf8().toStdString(),
                                static_cast<std::uint32_t>(color), fontSize});
        if (++count > 1000000) {
            result.error = QStringLiteral("弹幕条数超过 100 万限制");
            return result;
        }
        const int percent = static_cast<int>(100 * file.pos() / std::max<qint64>(1, file.size()));
        if (progress && percent != lastProgress) {
            progress(percent);
            lastProgress = percent;
        }
    }
    if (xml.hasError()) {
        result.items.clear();
        result.error = QStringLiteral("XML 第 %1 行：%2").arg(xml.lineNumber()).arg(xml.errorString());
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
    if (stop.stop_requested())
        result.cancelled = true;
    return result;
}
