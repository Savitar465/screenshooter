#pragma once

#include "core/services/ISettingsRepository.h"
#include <utility>

namespace qaflow {

/// Ajustes de usuario respaldados por QSettings (~/.config/QAflow/QAflow.conf en Linux).
/// Sólo el código Jira se guarda por proyecto; conexiones (gestor y GESREQ), credenciales y capturas son
/// generales. El sistema de GESREQ de cada proyecto no vive aquí sino en el catálogo de proyectos.
/// El token del gestor y la contraseña de GESREQ no se escriben aquí (van al ISecretStore); sólo se leen
/// si quedaron de una versión anterior o de un arranque sin llavero, para que SettingsStore los migre.
class QSettingsRepository : public ISettingsRepository {
public:
    explicit QSettingsRepository(QString projectId = {})
        : m_projectId(std::move(projectId)) {}
    TrackerSettings loadTracker() override;
    void saveTracker(const TrackerSettings& s) override;
    RequirementSourceSettings loadRequirementSource() override;
    void saveRequirementSource(const RequirementSourceSettings& s) override;
    AiSettings loadAi() override;
    void saveAi(const AiSettings& s) override;
    CaptureSettings loadCapture() override;
    void saveCapture(const CaptureSettings& s) override;
    AppSettings loadApp() override;
    void saveApp(const AppSettings& s) override;
    RunShortcuts loadRunShortcuts() override;
    void saveRunShortcuts(const RunShortcuts& s) override;
private:
    QString m_projectId;
};

} // namespace qaflow
