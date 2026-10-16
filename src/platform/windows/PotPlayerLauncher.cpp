#include "platform/windows/PotPlayerLauncher.h"
#include "infrastructure/ExternalPlayer.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QFile>
#include <windows.h>
#include <cstring>

namespace PotPlayerLauncher {
QString validate(const QString& path) {
    const QFileInfo info(path);
    if (path.contains(QChar::Null) || path.size() > 32768 || !info.isAbsolute() || !info.isFile()
        || info.fileName().compare("PotPlayerMini64.exe", Qt::CaseInsensitive) != 0)
        return QStringLiteral("请选择存在的 PotPlayerMini64.exe（64 位 PotPlayer）");
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return QStringLiteral("无法读取播放器程序：") + file.errorString();
    const auto read = [&file](qint64 offset, void* output, qint64 size) {
        return offset >= 0 && offset <= file.size() && size <= file.size() - offset
            && file.seek(offset) && file.read(static_cast<char*>(output), size) == size;
    };
    IMAGE_DOS_HEADER dos{}; IMAGE_FILE_HEADER header{}; DWORD signature{}; IMAGE_OPTIONAL_HEADER64 optional{};
    const auto invalid = QStringLiteral("程序不是有效的 Windows x64 可执行文件");
    if (!read(0, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos)
        || !read(dos.e_lfanew, &signature, sizeof(signature)) || signature != IMAGE_NT_SIGNATURE
        || !read(qint64(dos.e_lfanew) + 4, &header, sizeof(header))) return invalid;
    if (header.Machine != IMAGE_FILE_MACHINE_AMD64) return QStringLiteral("此预设仅支持 x64 PotPlayer，请选择 64 位版本");
    const qint64 optionalOffset = qint64(dos.e_lfanew) + 4 + sizeof(header);
    if (!(header.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) || (header.Characteristics & IMAGE_FILE_DLL)
        || header.NumberOfSections == 0 || header.NumberOfSections > 96 || header.SizeOfOptionalHeader < sizeof(optional)
        || !read(optionalOffset, &optional, sizeof(optional)) || optional.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC
        || optional.Subsystem != IMAGE_SUBSYSTEM_WINDOWS_GUI || optional.SizeOfHeaders > file.size()
        || optional.SizeOfImage <= optional.AddressOfEntryPoint) return invalid;
    const qint64 sections = optionalOffset + header.SizeOfOptionalHeader;
    if (optional.SizeOfHeaders < sections + qint64(header.NumberOfSections) * sizeof(IMAGE_SECTION_HEADER)) return invalid;
    bool executableEntry = false;
    for (int i = 0; i < header.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!read(sections + qint64(i) * sizeof(section), &section, sizeof(section))
            || quint64(section.PointerToRawData) + section.SizeOfRawData > quint64(file.size())) return invalid;
        executableEntry |= (section.Characteristics & IMAGE_SCN_MEM_EXECUTE) && optional.AddressOfEntryPoint >= section.VirtualAddress
            && quint64(optional.AddressOfEntryPoint) < quint64(section.VirtualAddress) + section.SizeOfRawData;
    }
    if (!executableEntry) return invalid;
    // Version identity is a compatibility check, not a signature/trust guarantee.
    const auto native = QDir::toNativeSeparators(info.canonicalFilePath()).toStdWString();
    DWORD ignored = 0; const DWORD size = GetFileVersionInfoSizeW(native.c_str(), &ignored);
    if (size > 4 * 1024 * 1024) return QStringLiteral("播放器版本资源异常");
    if (size) {
        QByteArray bytes(size, '\0');
        if (!GetFileVersionInfoW(native.c_str(), 0, size, bytes.data())) return QStringLiteral("无法读取播放器版本信息");
        struct Translation { WORD language, page; };
        Translation* translations = nullptr; UINT count = 0;
        bool hasIdentity = false, matches = false;
        if (VerQueryValueW(bytes.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &count)) {
            for (UINT i = 0; i < count / sizeof(Translation); ++i) {
                for (const auto* key : {"ProductName", "FileDescription", "OriginalFilename"}) {
                    const auto query = QString("\\StringFileInfo\\%1%2\\%3").arg(translations[i].language, 4, 16, QChar('0'))
                        .arg(translations[i].page, 4, 16, QChar('0')).arg(key).toStdWString();
                    wchar_t* value = nullptr; UINT length = 0;
                    if (VerQueryValueW(bytes.data(), query.c_str(), reinterpret_cast<void**>(&value), &length) && length > 1) {
                        hasIdentity = true; matches |= QString::fromWCharArray(value, length - 1).contains("PotPlayer", Qt::CaseInsensitive);
                    }
                }
            }
        }
        if (hasIdentity && !matches) return QStringLiteral("程序版本信息与 PotPlayer 预设不匹配");
    }
    return {};
}
QStringList discoverCandidates() {
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
    return candidates;
}
QString discover() {
    const auto candidates = discoverCandidates();
    return candidates.size() == 1 ? candidates.first() : QString();
}
QString preflight(const QString& path) {
    return ExternalPlayerProcess::preflight(path, {"PotPlayerMini64.exe", "PotPlayerMini.exe"});
}
}
