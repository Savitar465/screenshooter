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

    /// Sesiones de ciclos en pausa, aparcadas para dejar sitio a otra ejecución: una por ciclo.
    virtual std::optional<RunSession> loadSuspended(const QString& planRunId) = 0;
    virtual bool saveSuspended(const QString& planRunId, const RunSession& session) = 0;
    virtual void clearSuspended(const QString& planRunId) = 0;
};

} // namespace qaflow
