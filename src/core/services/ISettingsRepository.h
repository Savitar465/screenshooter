#pragma once

#include "core/models/Settings.h"

namespace qaflow {

class ISettingsRepository {
public:
    virtual ~ISettingsRepository() = default;

    virtual JiraSettings loadJira() = 0;
    virtual void saveJira(const JiraSettings& s) = 0;

    virtual CaptureSettings loadCapture() = 0;
    virtual void saveCapture(const CaptureSettings& s) = 0;
};

} // namespace qaflow
