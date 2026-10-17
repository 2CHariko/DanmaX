#include "platform/windows/PlayerProcessIdentity.h"
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include "platform/windows/VlcLauncher.h"
#include <QFileInfo>
#include <QByteArray>
namespace {
QString key(HANDLE handle, DWORD pid, bool potplayer = false) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(handle, &created, &exited, &kernel, &user)) return {};
    const quint64 stamp = (quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime;
    return QString("%1:%2:%3").arg(potplayer ? "danmax-potplayer" : "danmax-vlc").arg(pid).arg(stamp);
}
DWORD processId(const QString& session) {
    if (!session.startsWith("danmax-vlc:") && !session.startsWith("danmax-potplayer:")) return 0;
    bool ok = false; const auto value = session.section(':', 1, 1).toUInt(&ok);
    return ok ? value : 0;
}
}
namespace PlayerProcessIdentity {
QVariantList discoverVlc() {
    QVariantList result;
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return result;
    PROCESSENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) do {
        if (QString::fromWCharArray(entry.szExeFile).compare("vlc.exe", Qt::CaseInsensitive) != 0) continue;
        const auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, entry.th32ProcessID);
        if (!handle) continue;
        wchar_t path[32768]; DWORD length = 32768;
        QString executable, id;
        if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT && QueryFullProcessImageNameW(handle, 0, path, &length)) {
            executable = QString::fromWCharArray(path, length); id = key(handle, entry.th32ProcessID);
        }
        CloseHandle(handle);
        if (id.isEmpty() || !VlcLauncher::validate(executable).isEmpty()) continue;
        result.append(QVariantMap{{"id", id}, {"title", "VLC"}, {"pid", quint32(entry.th32ProcessID)},
            {"label", QStringLiteral("VLC · PID %1 · 未建立同步连接").arg(entry.th32ProcessID)},
            {"connectable", false}, {"reason", QStringLiteral("请将 VLC 设为默认外部播放器，再通过 DanmaX 打开视频建立同步连接。")}});
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return result;
}
QString session(qint64 pid, const QString& executable) {
    if (pid <= 0 || quint64(pid) > MAXDWORD) return {};
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, DWORD(pid));
    if (!handle) return {};
    QString result;
    wchar_t buffer[32768]; DWORD size = 32768;
    if (WaitForSingleObject(handle, 0) == WAIT_TIMEOUT && QueryFullProcessImageNameW(handle, 0, buffer, &size)
        && QFileInfo(QString::fromWCharArray(buffer, size)).canonicalFilePath().compare(QFileInfo(executable).canonicalFilePath(), Qt::CaseInsensitive) == 0)
        result = key(handle, DWORD(pid));
    CloseHandle(handle); return result;
}
bool alive(const QString& session, const QString& executable) {
    const auto pid = processId(session); if (!pid) return false;
    HANDLE handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!handle) return false;
    bool result = WaitForSingleObject(handle, 0) == WAIT_TIMEOUT && key(handle, pid, session.startsWith("danmax-potplayer:")) == session;
    if (result && !executable.isEmpty()) {
        wchar_t buffer[32768]; DWORD size = 32768;
        result = QueryFullProcessImageNameW(handle, 0, buffer, &size)
            && QFileInfo(QString::fromWCharArray(buffer, size)).canonicalFilePath().compare(QFileInfo(executable).canonicalFilePath(), Qt::CaseInsensitive) == 0;
    }
    CloseHandle(handle); return result;
}
bool matchesWindow(quintptr window, const QString& session) {
    DWORD pid = 0; GetWindowThreadProcessId(reinterpret_cast<HWND>(window), &pid);
    return pid && pid == processId(session) && alive(session);
}
Listener listener(quint16 port, const QString& session) {
    DWORD size = 0;
    if (GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != ERROR_INSUFFICIENT_BUFFER
        || size > 16 * 1024 * 1024) return Listener::Unavailable;
    QByteArray buffer(size, '\0');
    if (GetExtendedTcpTable(buffer.data(), &size, FALSE, AF_INET, TCP_TABLE_OWNER_PID_LISTENER, 0) != NO_ERROR) return Listener::Unavailable;
    const auto* table = reinterpret_cast<const MIB_TCPTABLE_OWNER_PID*>(buffer.constData());
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto& row = table->table[i];
        if (ntohs(USHORT(row.dwLocalPort)) != port) continue;
        if (row.dwOwningPid == processId(session) && row.dwLocalAddr == htonl(INADDR_LOOPBACK) && alive(session)) return Listener::Owned;
        // Wildcard bindings could receive our loopback request too.
        if (row.dwLocalAddr == htonl(INADDR_LOOPBACK) || row.dwLocalAddr == INADDR_ANY) return Listener::Foreign;
    }
    return Listener::Absent;
}
}
