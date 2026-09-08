#pragma once

#include "core/models/Settings.h"

namespace qaflow {

/// Ajustes no secretos. El token del gestor NO pasa por aquí: lo guarda ISecretStore.
/// `loadTracker()` puede devolver un token si quedó de una versión anterior (se migra y se borra).
class ISettingsRepository {
public:
    virtual ~ISettingsRepository() = default;

    virtual TrackerSettings loadTracker() = 0;
    virtual void saveTracker(const TrackerSettings& s) = 0;

    virtual CaptureSettings loadCapture() = 0;
    virtual void saveCapture(const CaptureSettings& s) = 0;

    virtual AppSettings loadApp() = 0;
    virtual void saveApp(const AppSettings& s) = 0;
};

} // namespace qaflow
