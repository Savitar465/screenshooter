#pragma once

#include "core/models/Requirement.h"
#include "core/models/Settings.h"

namespace qaflow {

/// Ajustes no secretos. El token del gestor y la contraseña de GESREQ NO pasan por aquí: los guarda
/// ISecretStore. `loadTracker()` y `loadRequirementSource()` pueden devolver el secreto si quedó en el
/// fichero (versión anterior o sin llavero): SettingsStore lo migra y lo borra.
class ISettingsRepository {
public:
    virtual ~ISettingsRepository() = default;

    virtual TrackerSettings loadTracker() = 0;
    virtual void saveTracker(const TrackerSettings& s) = 0;

    /// Conexión con GESREQ: común a todos los proyectos.
    virtual RequirementSourceSettings loadRequirementSource() = 0;
    virtual void saveRequirementSource(const RequirementSourceSettings& s) = 0;

    virtual CaptureSettings loadCapture() = 0;
    virtual void saveCapture(const CaptureSettings& s) = 0;

    virtual AppSettings loadApp() = 0;
    virtual void saveApp(const AppSettings& s) = 0;

    virtual RunShortcuts loadRunShortcuts() = 0;
    virtual void saveRunShortcuts(const RunShortcuts& s) = 0;
};

} // namespace qaflow
