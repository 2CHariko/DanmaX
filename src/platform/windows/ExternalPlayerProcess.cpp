#include "infrastructure/ExternalPlayer.h"
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <windows.h>
#include <tlhelp32.h>

namespace ExternalPlayerProcess {
QString preflight(const QString& path, const QStringList& processNames) {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return QStringLiteral("无法检查播放器实例，请稍后重试");
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    int count = 0;
    QString error;
    if (!Process32FirstW(snapshot, &entry)) error = QStringLiteral("无法枚举播放器进程");
    else do {
        const auto name = QString::fromWCharArray(entry.szExeFile);
        if (!processNames.contains(name, Qt::CaseInsensitive)) continue;
        ++count;
        const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        wchar_t buffer[32768]; DWORD size = 32768;
        if (!process || !QueryFullProcessImageNameW(process, 0, buffer, &size))
            error = QStringLiteral("无法确认已有播放器路径，请使用相同权限启动播放器");
        else if (QFileInfo(QString::fromWCharArray(buffer, static_cast<int>(size))).canonicalFilePath()
                     .compare(QFileInfo(path).canonicalFilePath(), Qt::CaseInsensitive) != 0)
            error = QStringLiteral("已有播放器与绑定程序路径不同，请先关闭其他安装的播放器");
        if (process) CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    if (count > 1) return QStringLiteral("存在多个播放器实例，请保留一个后重试");
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
    return process.startDetached() ? QString() : QStringLiteral("无法启动播放器：") + process.errorString();
}
}
