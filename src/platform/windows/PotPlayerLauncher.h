#pragma once
#include <QString>
#include <QStringList>

namespace PotPlayerLauncher {
QString discover();
QString validate(const QString& path);
QString preflight(const QString& path);
QString launch(const QString& path, const QStringList& arguments);
}
