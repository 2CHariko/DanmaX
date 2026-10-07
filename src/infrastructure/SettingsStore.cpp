#include "infrastructure/SettingsStore.h"
#include <QAccessibilityHints>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QSaveFile>
#include <QSettings>
#include <QStringDecoder>
#include <QStyleHints>
#include <cmath>
namespace {
struct Setting {
    const char* group;
    const char* key;
    QVariant initial;
    const char* comment;
};
const QList<Setting>& schema() {
    static const QList<Setting> fields{
        {"Appearance", "theme", "system",
         "应用主题：system=跟随系统，light=浅色，dark=深色。默认：system。\nWindows 高对比度启用时优先跟随系统，不强制浅色或深色。"},
        {"Appearance", "fontFamily", "Microsoft YaHei",
         "弹幕字体名称，默认：Microsoft YaHei（微软雅黑）。不能为空，最多 4096 字符。\n使用系统已安装字体；缺失字形由 Qt 和系统字体回退处理，不随程序分发字体。"},
        {"Appearance", "fontSize", 24,
         "普通弹幕的基准字号，单位：逻辑像素；整数 10～72，默认：24。\nXML 字号以 25 为基准按比例缩放，因此源文件中的小字和大字仍有区别。"},
        {"Appearance", "strokeWidth", 1,
         "弹幕文字描边宽度，单位：逻辑像素；整数 0～6，默认：1，0 表示不描边。"},
        {"Appearance", "opacity", 0.85,
         "弹幕不透明度，范围：0.05～1.0，默认：0.85（85%）。\n数值越小越透明；只影响弹幕，不改变控制面板的不透明度。"},
        {"Danmaku", "speed", 180,
         "滚动弹幕速度，单位：逻辑像素/秒；整数 30～1500，默认：180。\n暂停时位置冻结；修改速度不会重新加载 XML 或清空全部在屏弹幕。"},
        {"Danmaku", "fixedSeconds", 5.0,
         "顶部、底部固定弹幕的显示时长，单位：秒；范围：1～30，默认：5。\n暂停时间不计入寿命；缩短时长可能让已达到新时限的固定弹幕立即退场。"},
        {"Danmaku", "maxActive", 500,
         "最大在屏弹幕数量，整数 50～5000，默认：500。\n这是资源容量上限，不是流畅度保证；减少容量时保留较早在场对象。"},
        {"Danmaku", "maxTracks", 18,
         "最大轨道数量，整数 1～60，默认：18。\n实际可用轨道还受屏幕高度、字体、字号、描边和行距限制。"},
        {"Danmaku", "lineSpacing", 0.2,
         "轨道额外行距比例，范围：0～2，默认：0.2（20%）。\n该值是比例，不是像素数；值越大，同样高度内的可用轨道越少。"},
        {"Danmaku", "overlap", false,
         "轨道已满时是否允许重叠：true=允许，false=禁止，默认：false。\n无论是否开启都优先使用安全轨道；禁止重叠且无可用位置时丢弃新弹幕。"},
        {"Sync", "timeOffset", 0.0,
         "弹幕相对媒体的时间偏移，单位：秒；范围：-120～120，默认：0。\n正值提前显示，负值延后显示；支持小数，例如 0.5 表示提前半秒。"},
        {"Sync", "targetSession", "",
         "选定的 Windows SMTC 媒体会话 ID，默认：空字符串（未选择）。\n由播放页选择播放器后保存，必须匹配会话 ID，不是媒体标题。"},
        {"Window", "screenIndex", 0,
         "弹幕显示器索引，整数 0～32，默认：0（系统枚举的第一台）。\n按播放时的显示器列表选择；显示器断开或索引越界时使用可用显示器。"},
        {"Window", "onTop", 1,
         "覆盖窗置顶策略，整数 0～3，默认：1。\n0=不置顶；1=置顶；2=周期保持置顶；3=先取消再恢复置顶的兼容策略。\n不保证覆盖独占全屏、受保护视频或其他置顶窗口。"},
        {"Window", "foregroundOnly", false,
         "仅所选播放器在前台时显示：true=开启，false=关闭，默认：false。\n用于媒体同步模式；无法关联媒体会话与前台窗口时请关闭，独立播放不受此项限制。"},
        {"Diagnostics", "debug", false,
         "在覆盖层显示调试信息：true=显示，false=隐藏，默认：false。"},
        {"Diagnostics", "debugPosition", "top_left",
         "调试信息位置：top_left=左上，top_right=右上，bottom_left=左下，bottom_right=右下。\n默认：top_left。仅在 debug=true 时显示。"},
        {"Logging", "logLevel", "INFO",
         "日志最低级别：DEBUG、INFO、WARNING、ERROR，默认：INFO。\n按上述顺序过滤；DEBUG 最详细，ERROR 只保留错误。"},
        {"Logging", "logToFile", true,
         "是否写入 app.log：true=写入，false=只保留界面日志，默认：true。\n单文件上限 2 MiB，保留一份 app.log.1 轮转备份。"},
        {"Playback", "lastFile", "",
         "最近加载的 XML 路径，默认：空字符串；由加载文件操作自动保存。\n启动时只恢复路径显示，不自动加载或播放；文件移动后请重新选择。\n手动填写建议使用正斜杠，如 D:/弹幕/example.xml，含逗号或分号的路径必须保留双引号。"}};
    return fields;
}
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
    if (value.metaType().id() != QMetaType::QString)
        return {};
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
QVariant checkedIni(const Setting& field, QVariant raw) {
    if (field.initial.metaType().id() == QMetaType::Bool) {
        const auto text = raw.toString().trimmed().toLower();
        if (text != "true" && text != "false" && text != "1" && text != "0")
            return {};
        raw = text == "true" || text == "1";
    }
    return checked(QString::fromLatin1(field.key), raw, field.initial);
}
QString iniValue(const QVariant& value) {
    if (value.metaType().id() == QMetaType::Bool)
        return value.toBool() ? "true" : "false";
    if (value.metaType().id() == QMetaType::Int)
        return QString::number(value.toInt());
    if (value.metaType().id() == QMetaType::Double)
        return QLocale::c().toString(value.toDouble(), 'g', QLocale::FloatingPointShortest);
    auto text = value.toString();
    // QSettings reserves @ prefixes for typed values. @@ decodes to a literal @.
    if (text.startsWith('@'))
        text.prepend('@');
    QString encoded = "\"";
    bool hexadecimal = false;
    for (const auto ch : text) {
        const ushort n = ch.unicode();
        if (hexadecimal && ((n >= '0' && n <= '9') || (n >= 'a' && n <= 'f') || (n >= 'A' && n <= 'F'))) {
            encoded += "\\x" + QString::number(n, 16);
            continue;
        }
        hexadecimal = false;
        switch (n) {
        case '\0': encoded += "\\0"; hexadecimal = true; break;
        case '\a': encoded += "\\a"; break;
        case '\b': encoded += "\\b"; break;
        case '\f': encoded += "\\f"; break;
        case '\n': encoded += "\\n"; break;
        case '\r': encoded += "\\r"; break;
        case '\t': encoded += "\\t"; break;
        case '\v': encoded += "\\v"; break;
        case '\\': encoded += "\\\\"; break;
        case '"': encoded += "\\\""; break;
        default:
            if (n < 0x20) {
                encoded += "\\x" + QString::number(n, 16);
                hexadecimal = true;
            } else
                encoded += ch;
        }
    }
    return encoded + '"';
}
QByteArray commentedIni(const QVariantMap& values) {
    QString text = QStringLiteral(
        "; Local Danmaku 配置文件（UTF-8 编码）\n"
        "; 首次运行自动创建；界面修改即时生效并保存，数值输入合并 200 毫秒后写盘。\n"
        "; 手动修改前请退出所有程序实例；启动时读取，不监视外部文件修改。\n"
        "; 保存时重建程序自带的中文说明、分组与已知字段，不保留额外注释或未知字段。\n"
        "; 布尔值写 true/false（读取也接受 1/0），小数使用英文句点。\n"
        "; 字符串保留双引号；反斜杠写成两个，双引号写成反斜杠加双引号。\n"
        "; 字符串开头的 @ 在文件中写为 @@；文件路径建议使用正斜杠。\n"
        "; 缺失字段使用默认值；无效字段提示错误并使用默认值，首次覆盖前备份原文件。\n"
        "; 只读取本文件；旧 settings.json 和旧版 INI 不导入、不转换。\n\n"
        "[Meta]\n; 本格式版本：1。请勿删除此项；其他版本不自动迁移。\nformatVersion=1\n");
    QByteArray lastGroup;
    for (const auto& field : schema()) {
        if (lastGroup != field.group) {
            lastGroup = field.group;
            text += "\n[" + QString::fromLatin1(field.group) + "]\n";
        } else
            text += '\n';
        for (const auto& line : QString::fromUtf8(field.comment).split('\n'))
            text += "; " + line + '\n';
        text += QString::fromLatin1(field.key) + '=' + iniValue(values.value(field.key)) + '\n';
    }
    text.replace("\n", "\r\n");
    return text.toUtf8();
}
} // namespace
QVariantMap SettingsStore::defaults() {
    QVariantMap values;
    for (const auto& field : schema())
        values.insert(QString::fromLatin1(field.key), field.initial);
    return values;
}
SettingsStore::SettingsStore(QString directory, QObject* parent)
    : QObject(parent), values_(defaults()), path_(QDir(directory).filePath("settings.ini")) {
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(200);
    connect(&saveTimer_, &QTimer::timeout, this, [this] { save(); });
    QFile file(path_);
    const bool existing = file.exists();
    if (existing) {
        if (!file.open(QIODevice::ReadOnly))
            error_ = file.errorString();
        else if (file.size() > 1024 * 1024)
            error_ = QStringLiteral("INI 配置超过 1 MiB，已使用默认值；原文件保留。");
        else {
            const auto bytes = file.readAll();
            QStringDecoder utf8(QStringDecoder::Utf8);
            const QString decoded = utf8(bytes);
            Q_UNUSED(decoded);
            if (file.error() != QFileDevice::NoError)
                error_ = file.errorString();
            else if (utf8.hasError())
                error_ = QStringLiteral("INI 配置不是有效 UTF-8，已使用默认值；原文件保留。");
            else {
                QSettings ini(path_, QSettings::IniFormat);
                ini.setFallbacksEnabled(false);
                ini.sync();
                bool versionOk = false;
                const int version = ini.value("Meta/formatVersion").toInt(&versionOk);
                auto candidate = values_;
                QStringList invalid;
                for (const auto& field : schema()) {
                    const QString name = QString::fromLatin1(field.group) + '/' + field.key;
                    if (!ini.contains(name))
                        continue;
                    const auto value = checkedIni(field, ini.value(name));
                    if (value.isValid())
                        candidate[field.key] = value;
                    else
                        invalid.append(name);
                }
                if (ini.status() != QSettings::NoError || !versionOk || version != 1)
                    error_ = QStringLiteral("INI 格式或版本无效，已使用默认值；原文件保留，不进行兼容迁移。");
                else {
                    values_ = std::move(candidate);
                    if (!invalid.isEmpty())
                        error_ = QStringLiteral("以下配置无效，已使用默认值：") + invalid.join(QStringLiteral("、"));
                }
            }
        }
    }
    preserveOriginal_ = !error_.isEmpty();
    if (qGuiApp)
        connect(qGuiApp->styleHints()->accessibility(), &QAccessibilityHints::contrastPreferenceChanged, this,
                [this] { applyTheme(); });
    applyTheme();
    if (!existing)
        save();
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
    const QByteArray bytes = commentedIni(values_);
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
