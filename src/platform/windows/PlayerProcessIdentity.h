#pragma once
#include <QString>
namespace PlayerProcessIdentity {
QString session(qint64 pid, const QString& executable);
bool alive(const QString& session, const QString& executable = {});
bool matchesWindow(quintptr window, const QString& session);
enum class Listener { Absent, Owned, Foreign, Unavailable };
Listener listener(quint16 port, const QString& session);
}
