#pragma once

#include "core/services/IRunSessionRepository.h"

#include <QString>

namespace qaflow {

/// Ejecución en curso en `session.json` dentro del directorio de datos. Se borra al terminar.
class JsonRunSessionRepository : public IRunSessionRepository {
public:
    explicit JsonRunSessionRepository(const QString& dataDir);

    std::optional<RunSession> loadSession() override;
    bool saveSession(const RunSession& session) override;
    void clearSession() override;

private:
    QString m_path;
};

} // namespace qaflow
