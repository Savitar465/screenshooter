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
/// caso. Cada informe es único, así que cada ejecución publicada estrena su propio Test de Zephyr,
/// creado a partir del caso (título, precondiciones y pasos); la clave queda en la ejecución
/// (`RunRecord::testKey`) y sólo se reutiliza si ese mismo informe se publica otra vez. Un caso de
/// QAflow ejecutado en varios ciclos tiene, por tanto, un Test por ciclo.
class TestPublishService : public QObject {
    Q_OBJECT
public:
    TestPublishService(std::shared_ptr<ITestManagement> zephyr, TestCaseStore& cases, RunHistoryStore& history,
                       SettingsStore& settings, QObject* parent = nullptr);

    /// ¿Está configurada la publicación? (gestor Jira, conectado y Zephyr activado en Ajustes).
    bool enabled() const;
    /// Casos ejecutados del informe a cuya ejecución habrá que crearle el Test (los que no se
    /// publicaron nunca; una republicación reutiliza los Tests que ya tiene).
    QStringList casesNeedingTest(const PlanReport& report) const;

    void testConnection(std::function<void(const ConnectionResult&)> done);
    /// Publica las ejecuciones del informe como un ciclo nuevo y anota en el ciclo de plan el de
    /// Zephyr en el que quedaron, para saber después dónde se publicaron esos resultados.
    void publish(const PlanReport& report, std::function<void(const PublishResult&)> done);
    /// Vuelve a mandar los resultados al ciclo de Zephyr en el que el informe ya está publicado:
    /// las ejecuciones que ya tiene se actualizan (veredicto, pasos y las evidencias que falten), las
    /// que se quedaron fuera se añaden, y los Tests ya creados se reutilizan. Falla si no está publicado.
    void update(const PlanReport& report, std::function<void(const PublishResult&)> done);

    /// Enlace al ciclo de Zephyr en el que está publicado el informe (vacío si no lo está o no hay
    /// URL de Jira): la búsqueda de ejecuciones de ese ciclo.
    QString cycleUrl(const PlanReport& report) const;

    /// Traduce el informe a la petición que se enviará (público para poder probarlo y previsualizarlo).
    /// Con `update`, la petición apunta al ciclo de Zephyr en el que ya está publicado.
    PublishRequest requestFor(const PlanReport& report, bool update = false) const;

private:
    void send(const PlanReport& report, bool update, std::function<void(const PublishResult&)> done);

    std::shared_ptr<ITestManagement> m_zephyr;
    TestCaseStore& m_cases;
    RunHistoryStore& m_history;
    SettingsStore& m_settings;
};

} // namespace qaflow
