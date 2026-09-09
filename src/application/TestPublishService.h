#pragma once

#include "core/models/PlanReport.h"
#include "core/services/ITestManagement.h"

#include <QObject>
#include <memory>

namespace qaflow {

class TestCaseStore;
class SettingsStore;

/// Publica en la herramienta de gestión de pruebas (Zephyr) el resultado de un ciclo de plan:
/// traduce el informe del historial a lo que espera la herramienta y añade las evidencias de cada
/// caso. La correspondencia entre un caso de QAflow y su Test la da `TestCase::testKey`.
class TestPublishService : public QObject {
    Q_OBJECT
public:
    TestPublishService(std::shared_ptr<ITestManagement> zephyr, TestCaseStore& cases, SettingsStore& settings, QObject* parent = nullptr);

    /// ¿Está configurada la publicación? (gestor Jira, conectado y Zephyr activado en Ajustes).
    bool enabled() const;
    /// Casos del informe que no se van a publicar porque no tienen clave de Test.
    QStringList casesWithoutTestKey(const PlanReport& report) const;

    void testConnection(std::function<void(const ConnectionResult&)> done);
    /// Publica las ejecuciones del informe como un ciclo nuevo.
    void publish(const PlanReport& report, std::function<void(const PublishResult&)> done);

    /// Traduce el informe a la petición que se enviará (público para poder probarlo y previsualizarlo).
    PublishRequest requestFor(const PlanReport& report) const;

private:
    std::shared_ptr<ITestManagement> m_zephyr;
    TestCaseStore& m_cases;
    SettingsStore& m_settings;
};

} // namespace qaflow
