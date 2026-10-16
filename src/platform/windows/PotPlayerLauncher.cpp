#include "platform/windows/PotPlayerLauncher.h"
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <windows.h>
#include <tlhelp32.h>

namespace PotPlayerLauncher {
QString validate(const QString& path) {
    const QFileInfo file(path);
    if (!file.isAbsolute() || !file.isFile() ||
        file.fileName().compare(QStringLiteral("PotPlayerMini64.exe"), Qt::CaseInsensitive) != 0)
        return QStringLiteral("请选择存在的 PotPlayerMini64.exe（64 位 PotPlayer）");
    return {};
}
QString discover() {
    QStringList candidates;
    for (const auto* root : {"HKEY_CURRENT_USER", "HKEY_LOCAL_MACHINE"}) {
        for (const auto* view : {"Software", "Software\\WOW6432Node"}) {
            QSettings registry(QString::fromLatin1(root) + '\\' + QString::fromLatin1(view) +
                QStringLiteral("\\Microsoft\\Windows\\CurrentVersion\\App Paths\\PotPlayerMini64.exe"), QSettings::NativeFormat);
            auto path = registry.value(QStringLiteral(".")).toString().trimmed();
            if (path.startsWith('"') && path.endsWith('"')) path = path.mid(1, path.size() - 2);
            if (!validate(path).isEmpty()) continue;
            path = QFileInfo(path).canonicalFilePath();
            bool duplicate = false;
            for (const auto& existing : candidates) duplicate |= existing.compare(path, Qt::CaseInsensitive) == 0;
            if (!duplicate) candidates.push_back(path);
        }
    }
    return candidates.size() == 1 ? candidates.first() : QString();
}
QString preflight(const QString& path) {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return QStringLiteral("无法检查 PotPlayer 实例，请稍后重试");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    int count = 0;
    QString error;
    if (!Process32FirstW(snapshot, &entry)) error = QStringLiteral("无法枚举播放器进程");
    else do {
        const auto name = QString::fromWCharArray(entry.szExeFile);
        if (name.compare(QStringLiteral("PotPlayerMini64.exe"), Qt::CaseInsensitive) != 0 &&
            name.compare(QStringLiteral("PotPlayerMini.exe"), Qt::CaseInsensitive) != 0) continue;
        ++count;
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        wchar_t buffer[32768]; DWORD size = 32768;
        if (!process || !QueryFullProcessImageNameW(process, 0, buffer, &size))
            error = QStringLiteral("无法确认已有 PotPlayer 路径，请使用相同权限启动播放器");
        else if (QFileInfo(QString::fromWCharArray(buffer, static_cast<int>(size))).canonicalFilePath()
                     .compare(QFileInfo(path).canonicalFilePath(), Qt::CaseInsensitive) != 0)
            error = QStringLiteral("已有 PotPlayer 与绑定程序路径不同，请先关闭其他安装的播放器");
        if (process) CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    if (count > 1) return QStringLiteral("存在多个 PotPlayer 实例，请保留一个后重试");
    return error;
}
QString launch(const QString& path, const QStringList& arguments) {
    QProcess process;
    process.setProgram(path);
    process.setArguments(arguments);
    process.setWorkingDirectory(QFileInfo(path).absolutePath());
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        // The detached GUI player must not retain DanmaX's logs or parent pipes.
        arguments->inheritHandles = false;
    });
    auto environment = QProcessEnvironment::systemEnvironment();
    for (const auto* key : {"QTDIR", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"})
        environment.remove(QString::fromLatin1(key));
    process.setProcessEnvironment(environment);
    return process.startDetached() ? QString() : QStringLiteral("无法启动 PotPlayer：") + process.errorString();
}
}
