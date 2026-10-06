#include "infrastructure/SettingsStore.h"
#include <QAccessibilityHints>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSettings>
#include <QStyleHints>
#include <cmath>
namespace {
QVariant checked(const QString& key, const QVariant& value, const QVariant& fallback) {
    static const QMap<QString, QPair<double, double>> ranges{
        {"fontSize", {10, 72}},   {"strokeWidth", {0, 6}},   {"opacity", {0.05, 1}},
        {"speed", {30, 1500}},    {"fixedSeconds", {1, 30}}, {"maxActive", {50, 5000}},
        {"maxTracks", {1, 60}},   {"lineSpacing", {0, 2}},   {"timeOffset", {-120, 120}},
        {"screenIndex", {0, 32}}, {"onTop", {0, 3}}};
    if (ranges.contains(key)) {
        bool ok;
        const double n = value.toDouble(&ok);
        const auto r = ranges[key];
        if (!ok || !std::isfinite(n) || n < r.first || n > r.second)
            return {};
        if (fallback.metaType().id() == QMetaType::Int)
            return std::floor(n) == n ? QVariant(static_cast<int>(n)) : QVariant{};
        return n;
    }
    if (fallback.metaType().id() == QMetaType::Bool)
        return value.metaType().id() == QMetaType::Bool ? value : QVariant{};
    const auto text = value.toString();
    if (key == "theme" && text != "system" && text != "light" && text != "dark")
        return {};
    if (key == "debugPosition" &&
        !QStringList{"top_left", "top_right", "bottom_left", "bottom_right"}.contains(text))
        return {};
    if (key == "logLevel" && !QStringList{"DEBUG", "INFO", "WARNING", "ERROR"}.contains(text))
        return {};
    if (text.size() > 4096 || (key == "fontFamily" && text.trimmed().isEmpty()))
        return {};
    return text;
}
} // namespace
QVariantMap SettingsStore::defaults() {
    return {{"fontFamily", "Microsoft YaHei"},
            {"fontSize", 24},
            {"strokeWidth", 1},
            {"opacity", 0.85},
            {"speed", 180},
            {"fixedSeconds", 5.0},
            {"maxActive", 500},
            {"maxTracks", 18},
            {"lineSpacing", 0.2},
            {"overlap", false},
            {"targetSession", ""},
            {"lastFile", ""},
            {"debug", false},
            {"debugPosition", "top_left"},
            {"onTop", 1},
            {"foregroundOnly", false},
            {"screenIndex", 0},
            {"theme", "system"},
            {"timeOffset", 0.0},
            {"logLevel", "INFO"},
            {"logToFile", true}};
}
SettingsStore::SettingsStore(QString directory, QObject* parent)
    : QObject(parent), values_(defaults()), path_(QDir(directory).filePath("settings.json")) {
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(200);
    connect(&saveTimer_, &QTimer::timeout, this, [this] { save(); });
    QFile file(path_);
    if (file.exists()) {
        if (!file.open(QIODevice::ReadOnly))
            error_ = file.errorString();
        else {
            QJsonParseError parseError;
            const auto doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            const auto root = doc.object();
            if (parseError.error != QJsonParseError::NoError || !doc.isObject() ||
                root.value("version").toInt() != 1 || !root.value("values").isObject())
                error_ =
                    QStringLiteral("配置格式或版本无效，已使用默认值；原文件保留。修改设置前请备份原文件。");
            else {
                const auto input = root.value("values").toObject().toVariantMap();
                for (auto it = input.begin(); it != input.end(); ++it)
                    if (values_.contains(it.key())) {
                        const auto v = checked(it.key(), it.value(), values_[it.key()]);
                        if (v.isValid())
                            values_[it.key()] = v;
                        else
                            error_ = QStringLiteral("部分配置超出范围，已使用默认值");
                    }
            }
        }
    }
    preserveOriginal_ = !error_.isEmpty();
    if (qGuiApp)
        connect(qGuiApp->styleHints()->accessibility(), &QAccessibilityHints::contrastPreferenceChanged, this,
                [this] { applyTheme(); });
    applyTheme();
}
SettingsStore::~SettingsStore() {
    flushPending();
}
void SettingsStore::flushPending() {
    if (saveTimer_.isActive())
        save();
}
void SettingsStore::applyTheme() {
    if (!qGuiApp)
        return;
    const auto theme = values_["theme"].toString();
    if (theme == "system" ||
        qGuiApp->styleHints()->accessibility()->contrastPreference() == Qt::ContrastPreference::HighContrast)
        qGuiApp->styleHints()->unsetColorScheme();
    else
        qGuiApp->styleHints()->setColorScheme(theme == "dark" ? Qt::ColorScheme::Dark
                                                              : Qt::ColorScheme::Light);
}
bool SettingsStore::save() {
    saveTimer_.stop();
    if (preserveOriginal_ && QFileInfo::exists(path_)) {
        const QString backup =
            path_ + ".backup-" + QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz");
        if (!QFile::copy(path_, backup)) {
            error_ = QStringLiteral("配置备份失败，未覆盖原文件");
            emit errorChanged();
            return false;
        }
    }
    preserveOriginal_ = false;
    QSaveFile file(path_);
    const QByteArray bytes =
        QJsonDocument(QJsonObject{{"version", 1}, {"values", QJsonObject::fromVariantMap(values_)}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        error_ = file.errorString();
        emit errorChanged();
        return false;
    }
    error_.clear();
    emit errorChanged();
    return true;
}
bool SettingsStore::setValue(const QString& key, const QVariant& value) {
    if (!values_.contains(key)) {
        error_ = QStringLiteral("未知设置项：") + key;
        emit errorChanged();
        return false;
    }
    const auto v = checked(key, value, defaults()[key]);
    if (!v.isValid()) {
        error_ = QStringLiteral("设置值无效：") + key;
        emit errorChanged();
        return false;
    }
    if (values_[key] == v)
        return error_.isEmpty() ? true : save();
    const bool debounce = error_.isEmpty() && (defaults()[key].metaType().id() == QMetaType::Int ||
                                               defaults()[key].metaType().id() == QMetaType::Double);
    values_[key] = v;
    applyTheme();
    emit changed();
    if (debounce) {
        saveTimer_.start();
        return true;
    }
    return save();
}
bool SettingsStore::reset() {
    values_ = defaults();
    applyTheme();
    emit changed();
    return save();
}
bool SettingsStore::retrySave() {
    return save();
}
bool SettingsStore::importIni(const QString& path) {
    if (!QFileInfo::exists(path)) {
        error_ = QStringLiteral("INI 文件不存在");
        emit errorChanged();
        return false;
    }
    QSettings ini(path, QSettings::IniFormat);
    const QMap<QString, QString> map{{"Display/font_name", "fontFamily"},
                                     {"Display/font_size", "fontSize"},
                                     {"Display/stroke_width", "strokeWidth"},
                                     {"Display/opacity", "opacity"},
                                     {"Display/max_tracks", "maxTracks"},
                                     {"Display/line_spacing_ratio", "lineSpacing"},
                                     {"Danmaku/scroll_speed", "speed"},
                                     {"Danmaku/max_danmaku_count", "maxActive"},
                                     {"Danmaku/allow_overlap", "overlap"},
                                     {"Sync/target_aumid", "targetSession"},
                                     {"Debug/enabled", "debug"},
                                     {"Debug/info_position", "debugPosition"},
                                     {"OnTopStrategy/method", "onTop"},
                                     {"Logging/level", "logLevel"},
                                     {"Logging/log_to_file", "logToFile"}};
    auto candidate = values_;
    for (auto it = map.begin(); it != map.end(); ++it)
        if (ini.contains(it.key())) {
            auto raw = ini.value(it.key());
            if (candidate[it.value()].metaType().id() == QMetaType::Bool) {
                const auto text = raw.toString().trimmed().toLower();
                if (text != "true" && text != "false" && text != "1" && text != "0")
                    continue;
                raw = text == "true" || text == "1";
            }
            auto v = checked(it.value(), raw, defaults()[it.value()]);
            if (v.isValid())
                candidate[it.value()] = v;
        }
    if (ini.contains("Danmaku/fixed_duration_ms")) {
        auto v = checked("fixedSeconds", ini.value("Danmaku/fixed_duration_ms").toDouble() / 1000,
                         defaults()["fixedSeconds"]);
        if (v.isValid())
            candidate["fixedSeconds"] = v;
    }
    if (ini.status() != QSettings::NoError) {
        error_ = QStringLiteral("INI 无法解析");
        emit errorChanged();
        return false;
    }
    values_ = candidate;
    applyTheme();
    emit changed();
    return save();
}
