#include "infrastructure/ExternalPlayer.h"
#include "platform/windows/PotPlayerLauncher.h"
#include <QFileInfo>

namespace {
class PotPlayerAdapter final : public IExternalPlayerAdapter {
  public:
    QString id() const override { return "potplayer"; }
    QString displayName() const override { return "PotPlayer"; }
    QStringList discoverCandidates() const override { return PotPlayerLauncher::discoverCandidates(); }
    QString executableFilter() const override { return QStringLiteral("PotPlayer (PotPlayerMini64.exe)"); }
    bool canDiscover() const override { return true; }
    QString discover() const override { return PotPlayerLauncher::discover(); }
    QString validate(const QString& path) const override { return PotPlayerLauncher::validate(path); }
    QString preflight(const ExternalPlayerProfile& profile) const override {
        return PotPlayerLauncher::preflight(profile.executable);
    }
    QStringList arguments(const ExternalPlayerProfile&, const QString& file) const override { return {file, "/current"}; }
    QString sessionId(const ExternalPlayerProfile&) const override { return "PotPlayerMini64.exe"; }
};
}
ExternalPlayerRegistry::ExternalPlayerRegistry() {
    add(std::make_unique<PotPlayerAdapter>());

}
bool ExternalPlayerRegistry::add(std::unique_ptr<IExternalPlayerAdapter> adapter) {
    if (!adapter || adapter->id().isEmpty() || find(adapter->id())) return false;
    adapters_.push_back(std::move(adapter)); return true;
}
const IExternalPlayerAdapter* ExternalPlayerRegistry::find(const QString& id) const {
    for (const auto& adapter : adapters_) if (adapter->id() == id) return adapter.get();
    return nullptr;
}
QList<ExternalPlayerProfile> ExternalPlayerRegistry::presets() const {
    QList<ExternalPlayerProfile> result;
    for (const auto& adapter : adapters_) {
        ExternalPlayerProfile p; p.id = adapter->id(); p.adapter = p.id;
        p.name = adapter->displayName();
        result.append(p);
    }
    return result;
}
