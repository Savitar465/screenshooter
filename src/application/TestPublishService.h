#pragma once

#include "core/models/PlanReport.h"
#include "core/services/ITestManagement.h"

#include <QObject>
#include <memory>

namespace qaflow {

class BugStore;
class IssueStore;
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
                       SettingsStore& settings, BugStore& bugs, QObject* parent = nullptr);

    /// Los issues del proyecto: con ellos el ciclo se publica con el número de requerimiento de su
    /// control de calidad. Se pone aparte porque el libro de issues se crea después que esto; sin él
    /// el ciclo sale con el nombre del plan, como antes de que los ciclos tuvieran issue.
    void setIssues(const IssueStore* issues) { m_issues = issues; }

    /// ¿Está configurada la publicación? (gestor Jira, conectado y Zephyr activado en Ajustes).
    bool enabled() const;

    /// Nombre con el que el ciclo se crea en Zephyr: el requerimiento, la revisión del control de
    /// calidad, el plan, la fecha y el ambiente («GREQ 2026997 · Rev. 2 · Regresión · 12/05/2026 · QA»).
    /// Lo que el ciclo no diga, no sale; un ciclo suelto se queda con el plan y la fecha de siempre.
    QString cycleName(const PlanReport& report) const;
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

    /// Bugs reportados desde ese caso durante ese ciclo, con el paso del que salieron. Se enlazan a la
    /// ejecución y a su paso en Zephyr, que es donde se buscan los defectos de una prueba.
    QList<PublishDefect> defectsOf(const PlanReport& report, const QString& caseId) const;
    /// Cuántas continuaciones lleva encadenadas el ciclo (0 = no es una continuación).
    int continuationDepth(const PlanRun& plan) const;

    std::shared_ptr<ITestManagement> m_zephyr;
    const IssueStore* m_issues = nullptr;
    TestCaseStore& m_cases;
    RunHistoryStore& m_history;
    SettingsStore& m_settings;
    BugStore& m_bugs;
};

} // namespace qaflow
