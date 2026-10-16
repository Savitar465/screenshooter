#pragma once

#include "core/services/IRunSessionRepository.h"

#include <QString>

namespace qaflow {

/// Ejecución en curso en `session.json` dentro del directorio de datos. Se borra al terminar. Los
/// ciclos en pausa aparcan la suya en `sessions/<id del ciclo>.json` hasta que se reanudan.
class JsonRunSessionRepository : public IRunSessionRepository {
public:
    explicit JsonRunSessionRepository(const QString& dataDir);

    std::optional<RunSession> loadSession() override;
    bool saveSession(const RunSession& session) override;
    void clearSession() override;
    std::optional<RunSession> loadSuspended(const QString& planRunId) override;
    bool saveSuspended(const QString& planRunId, const RunSession& session) override;
    void clearSuspended(const QString& planRunId) override;

private:
    QString suspendedPath(const QString& planRunId) const;
    static std::optional<RunSession> read(const QString& path);
    static bool write(const QString& path, const RunSession& session);

    QString m_path;
    QString m_dir;
};

} // namespace qaflow
