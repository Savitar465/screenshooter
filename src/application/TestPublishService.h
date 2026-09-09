#pragma once

#include "core/models/PlanReport.h"
#include "core/services/ITestManagement.h"

#include <QObject>
#include <memory>

namespace qaflow {

class TestCaseStore;
class RunHistoryStore;
class SettingsStore;

/// Publica en la herramienta de gestión de pruebas (Zephyr) el resultado de un ciclo de plan:
/// traduce el informe del historial a lo que espera la herramienta y añade las evidencias de cada
/// caso. El caso de QAflow es reutilizable —se ejecuta muchas veces y en varios planes—, así que su
/// Test de Zephyr es siempre el mismo: `TestCase::testKey` lo enlaza. Si el caso todavía no lo tiene,
/// la publicación lo estrena a partir del caso (título, precondiciones y pasos) y guarda ahí su clave.
class TestPublishService : public QObject {
    Q_OBJECT
public:
    TestPublishService(std::shared_ptr<ITestManagement> zephyr, TestCaseStore& cases, RunHistoryStore& history,
                       SettingsStore& settings, QObject* parent = nullptr);

    /// ¿Está configurada la publicación? (gestor Jira, conectado y Zephyr activado en Ajustes).
    bool enabled() const;
    /// Casos ejecutados del informe a los que habrá que crearles el Test porque aún no lo tienen.
    QStringList casesNeedingTest(const PlanReport& report) const;

    void testConnection(std::function<void(const ConnectionResult&)> done);
    /// Publica las ejecuciones del informe como un ciclo nuevo y anota en el ciclo de plan el de
    /// Zephyr en el que quedaron, para saber después dónde se publicaron esos resultados.
    void publish(const PlanReport& report, std::function<void(const PublishResult&)> done);

    /// Traduce el informe a la petición que se enviará (público para poder probarlo y previsualizarlo).
    PublishRequest requestFor(const PlanReport& report) const;

private:
    std::shared_ptr<ITestManagement> m_zephyr;
    TestCaseStore& m_cases;
    RunHistoryStore& m_history;
    SettingsStore& m_settings;
};

} // namespace qaflow
