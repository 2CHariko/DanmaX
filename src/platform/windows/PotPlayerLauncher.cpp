#include "platform/windows/PotPlayerLauncher.h"
#include "platform/windows/PlayerExecutable.h"
#include "infrastructure/ExternalPlayer.h"
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QFile>
#include <windows.h>
#include <cstring>

namespace PotPlayerLauncher {
QString validate(const QString& path) { return PlayerExecutable::validate(path, "PotPlayerMini64.exe", "PotPlayer"); }
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
