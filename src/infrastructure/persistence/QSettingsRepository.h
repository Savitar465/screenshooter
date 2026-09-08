#pragma once

#include "core/services/ISettingsRepository.h"

namespace qaflow {

/// Ajustes de usuario respaldados por QSettings (~/.config/QAflow/QAflow.conf en Linux).
/// El token del gestor no se escribe aquí (va al ISecretStore); sólo se lee si quedó de una
/// versión anterior, para que SettingsStore lo migre.
class QSettingsRepository : public ISettingsRepository {
public:
    TrackerSettings loadTracker() override;
    void saveTracker(const TrackerSettings& s) override;
    CaptureSettings loadCapture() override;
    void saveCapture(const CaptureSettings& s) override;
    AppSettings loadApp() override;
    void saveApp(const AppSettings& s) override;
    RunShortcuts loadRunShortcuts() override;
    void saveRunShortcuts(const RunShortcuts& s) override;
};

} // namespace qaflow
