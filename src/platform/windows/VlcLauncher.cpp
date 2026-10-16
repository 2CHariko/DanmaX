#include "platform/windows/VlcLauncher.h"
#include "platform/windows/PlayerExecutable.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
namespace VlcLauncher {
QString validate(const QString& path) {
    const auto issue = PlayerExecutable::validate(path, "vlc.exe", "VLC", 3);
    if (!issue.isEmpty()) return issue;
    const QDir root(QFileInfo(path).absolutePath());
    for (const auto* file : {"lua/intf/http.luac", "lua/intf/modules/httprequests.luac"}) {
        const QFileInfo dependency(root.filePath(QString::fromLatin1(file)));
        if (!dependency.isFile() || !dependency.isReadable()) return QStringLiteral("VLC 安装缺少 Lua HTTP 模块，请选择完整的 VLC 3.x 安装");
    }
    return {};
}
QStringList discoverCandidates() {
    QStringList result;
    for (const auto* root : {"HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE"}) for (const auto* view : {"Software", "Software\\WOW6432Node"}) {
        QSettings key(QString::fromLatin1(root) + '\\' + QString::fromLatin1(view) +
            "\\Microsoft\\Windows\\CurrentVersion\\App Paths\\vlc.exe", QSettings::NativeFormat);
        auto path = key.value(".").toString().trimmed();
        if (path.startsWith('"') && path.endsWith('"')) path = path.mid(1, path.size() - 2);
        if (!validate(path).isEmpty()) continue;
        path = QFileInfo(path).canonicalFilePath();
        if (!result.contains(path, Qt::CaseInsensitive)) result.append(path);
    }
    return result;
}
}
