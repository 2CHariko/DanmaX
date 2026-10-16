#pragma once
#include <QString>
#include <QStringList>

namespace PotPlayerLauncher {
QString discover();
QStringList discoverCandidates();
QString validate(const QString& path);
QString preflight(const QString& path);
}
