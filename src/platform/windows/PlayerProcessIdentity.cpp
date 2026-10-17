#include "platform/windows/PlayerProcessIdentity.h"
#include <winsock2.h>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include "platform/windows/VlcLauncher.h"
#include <QFileInfo>
#include <QByteArray>
#include <set>
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
QString closeNormally(const QString& session, const QString& executable) {
    const auto pid = processId(session);
    if (!pid || executable.isEmpty()) return QStringLiteral("VLC 关闭身份无效");
    const auto handle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!handle) {
        if (GetLastError() == ERROR_INVALID_PARAMETER) return {};
        return QStringLiteral("无法核实 VLC 进程是否退出");
    }
    struct Guard { HANDLE value; ~Guard() { CloseHandle(value); } } guard{handle};
    if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) return {};
    // Keep this exact process object alive through enumeration and exit waiting.
    // PID reuse cannot redirect WM_CLOSE to a replacement instance.
    if (key(handle, pid) != session) return QStringLiteral("VLC 进程身份已改变，未发送关闭请求");
    wchar_t path[32768]; DWORD size = 32768;
    if (!QueryFullProcessImageNameW(handle, 0, path, &size)
        || QFileInfo(QString::fromWCharArray(path, size)).canonicalFilePath().compare(
            QFileInfo(executable).canonicalFilePath(), Qt::CaseInsensitive) != 0)
        return QStringLiteral("VLC 程序路径校验失败，未发送关闭请求");
    struct Context { DWORD pid; HANDLE process; std::set<HWND> posted; } context{pid, handle};
    const auto start = GetTickCount64();
    while (GetTickCount64() - start < 5000) {
        if (WaitForSingleObject(handle, 0) == WAIT_OBJECT_0) return {};
        EnumWindows([](HWND window, LPARAM data) -> BOOL {
            auto& value = *reinterpret_cast<Context*>(data);
            DWORD owner{}; GetWindowThreadProcessId(window, &owner);
            if (owner == value.pid && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)
                && !value.posted.contains(window) && WaitForSingleObject(value.process, 0) == WAIT_TIMEOUT) {
                // VLC may expose a separate video window before its main UI.
                // Closing only the first window can stop video but leave VLC alive.
                if (PostMessageW(window, WM_CLOSE, 0, 0)) value.posted.insert(window);
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&context));
        if (WaitForSingleObject(handle, 50) == WAIT_OBJECT_0) return {};
    }
    return !context.posted.empty() ? QStringLiteral("VLC 尚未退出，请在播放器中完成关闭；未强制终止进程")
        : QStringLiteral("未找到可正常关闭的 VLC 主窗口，播放器仍可能运行");
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
