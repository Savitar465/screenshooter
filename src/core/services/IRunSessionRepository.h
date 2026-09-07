#pragma once

#include "core/models/TestRun.h"

#include <optional>

namespace qaflow {

/// Persistencia de la ejecución en curso (una sola). La implementación concreta vive en infrastructure/.
class IRunSessionRepository {
public:
    virtual ~IRunSessionRepository() = default;

    virtual std::optional<RunSession> loadSession() = 0;
    virtual bool saveSession(const RunSession& session) = 0;
    virtual void clearSession() = 0;
};

} // namespace qaflow
