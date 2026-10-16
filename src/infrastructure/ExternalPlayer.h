#pragma once
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

struct ExternalPlayerProfile {
    QString id = "potplayer";
    QString adapter = "potplayer";
    QString name = "PotPlayer";
    QString executable;
    bool operator==(const ExternalPlayerProfile&) const = default;
};

// Runtime port: platform discovery and player-specific launch/binding rules do
// not belong to the domain clock, renderer, or the video-open transaction.
class IExternalPlayerAdapter {
  public:
    virtual ~IExternalPlayerAdapter() = default;
    virtual QString id() const = 0;
    virtual QString displayName() const { return id(); }
    virtual QString executableFilter() const { return QStringLiteral("播放器 (*.exe)"); }
    virtual bool canDiscover() const { return false; }
    virtual QString discover() const { return {}; }
    virtual QStringList discoverCandidates() const { const auto path = discover(); return path.isEmpty() ? QStringList{} : QStringList{path}; }
    virtual QString validate(const QString& executable) const = 0;
    virtual QString preflight(const ExternalPlayerProfile& profile) const = 0;
    virtual QStringList arguments(const ExternalPlayerProfile& profile, const QString& file) const = 0;
    virtual QString sessionId(const ExternalPlayerProfile& profile) const = 0;
};

class ExternalPlayerRegistry {
  public:
    ExternalPlayerRegistry();
    bool add(std::unique_ptr<IExternalPlayerAdapter> adapter);
    const IExternalPlayerAdapter* find(const QString& id) const;
    QList<ExternalPlayerProfile> presets() const;
  private:
    std::vector<std::unique_ptr<IExternalPlayerAdapter>> adapters_;
};

namespace ExternalPlayerProcess {
QString launch(const QString& executable, const QStringList& arguments);
QString preflight(const QString& executable, const QStringList& processNames);
}
