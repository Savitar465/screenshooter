#pragma once

#include "core/services/ISettingsRepository.h"

namespace qaflow {

/// Ajustes de usuario respaldados por QSettings (~/.config/QAflow/QAflow.conf en Linux).
class QSettingsRepository : public ISettingsRepository {
public:
    JiraSettings loadJira() override;
    void saveJira(const JiraSettings& s) override;
    CaptureSettings loadCapture() override;
    void saveCapture(const CaptureSettings& s) override;
};

} // namespace qaflow
