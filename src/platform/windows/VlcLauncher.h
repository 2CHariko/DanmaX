#pragma once
#include <QStringList>
namespace VlcLauncher {
QString validate(const QString& path);
QStringList discoverCandidates();
}
