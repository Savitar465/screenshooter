#pragma once

#include "core/services/IUpdateSource.h"

namespace qaflow {

/// Preferencias del buscador de actualizaciones en QSettings, grupo "updates".
class QSettingsUpdatePreferences : public IUpdatePreferencesRepository {
public:
    UpdatePreferences load() override;
    void save(const UpdatePreferences& p) override;
};

} // namespace qaflow
