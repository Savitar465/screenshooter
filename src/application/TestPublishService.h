#pragma once

#include "core/models/PlanReport.h"
#include "core/services/ITestManagement.h"

#include <QObject>
#include <memory>

namespace qaflow {

class BugStore;
class IssueStore;
struct Issue;
class TestCaseStore;
class RunHistoryStore;
class SettingsStore;

/// Publica en la herramienta de gestión de pruebas (Zephyr) el resultado de un ciclo de plan:
/// traduce el informe del historial a lo que espera la herramienta y añade las evidencias de cada
/// caso.
///
/// **Un ciclo que prueba un issue** no crea un ciclo de Zephyr propio: va al **ciclo de su fase**
/// («GREQ 2026997 · QA»), el mismo para todas las rondas y continuaciones de esa fase, que actualizan
/// sus ejecuciones. Y cada caso del issue tiene **un Test**, el mismo en QA y en PRE
/// (`Issue::zephyr`): se crea la primera vez (o antes, con `createTests`) y se reutiliza siempre.
///
/// Un ciclo suelto (sin issue) sigue como antes: su propio ciclo de Zephyr y un Test por ejecución
/// (`RunRecord::testKey`), reutilizado sólo al volver a publicar ese mismo informe.
class TestPublishService : public QObject {
    Q_OBJECT
public:
    TestPublishService(std::shared_ptr<ITestManagement> zephyr, TestCaseStore& cases, RunHistoryStore& history,
                       SettingsStore& settings, BugStore& bugs, QObject* parent = nullptr);

    /// Los issues del proyecto: con ellos el ciclo de un issue va al ciclo de su fase y reutiliza sus
    /// Tests, y ahí se guardan los que se crean. Se pone aparte porque el libro de issues se crea
    /// después que esto; sin él todos los ciclos se publican como sueltos.
    void setIssues(IssueStore* issues) { m_issues = issues; }

    /// El issue cuyo control de calidad publica el ciclo; nullptr en un ciclo suelto.
    const Issue* issueOf(const PlanReport& report) const;
    /// ¿Va el informe al ciclo de Zephyr de su fase (prueba un issue) en vez de a uno propio?
    bool sharesPhaseCycle(const PlanReport& report) const { return issueOf(report) != nullptr; }
    /// Fase del ciclo de un issue: su ambiente o, si no lo dijo, la de su ronda.
    QString phaseOf(const PlanReport& report) const;
    /// Nombre del ciclo de una fase del issue: «GREQ 2026997 · QA».
    QString phaseCycleName(const Issue& issue, const QString& phase) const;
    /// De esos casos del issue, los que todavía no tienen Test en Zephyr.
    QStringList casesWithoutTest(const QString& issueId, const QStringList& caseIds) const;
    /// Crea en Zephyr los Tests que les faltan a esos casos del issue, sin ciclo, y los guarda en él:
    /// son los que se enlazan a su issue del gestor y los que usarán todos sus ciclos.
    void createTests(const QString& issueId, const QStringList& caseIds, std::function<void(const PublishResult&)> done);

    /// ¿Está configurada la publicación? (gestor Jira, conectado y Zephyr activado en Ajustes).
    bool enabled() const;

    /// Nombre del ciclo de Zephyr al que va el informe: el de su fase si prueba un issue («GREQ 2026997
    /// · QA») y, si es suelto, el plan y la fecha (más lo que diga de revisión, continuación y ambiente).
    QString cycleName(const PlanReport& report) const;
    /// Casos ejecutados del informe a los que habrá que crearles el Test: los que no lo tienen todavía
    /// (en su issue o, si es suelto, en su ejecución).
    QStringList casesNeedingTest(const PlanReport& report) const;

    void testConnection(std::function<void(const ConnectionResult&)> done);
    /// Publica las ejecuciones del informe y anota en el ciclo de plan el de Zephyr en el que quedaron.
    /// Uno suelto crea un ciclo nuevo; el de un issue va al de su fase (creándolo la primera vez).
    void publish(const PlanReport& report, std::function<void(const PublishResult&)> done);
    /// Vuelve a mandar los resultados al ciclo de Zephyr en el que el informe ya está publicado:
    /// las ejecuciones que ya tiene se actualizan (veredicto, pasos y las evidencias que falten), las
    /// que se quedaron fuera se añaden, y los Tests ya creados se reutilizan. Falla si un ciclo suelto no
    /// está publicado; el de un issue va siempre al de su fase.
    void update(const PlanReport& report, std::function<void(const PublishResult&)> done);

    /// Enlace al ciclo de Zephyr en el que está publicado el informe (vacío si no lo está o no hay
    /// URL de Jira): la búsqueda de ejecuciones de ese ciclo.
    QString cycleUrl(const PlanReport& report) const;

    /// Traduce el informe a la petición que se enviará (público para poder probarlo y previsualizarlo).
    /// Con `update`, la petición apunta al ciclo de Zephyr en el que ya está publicado.
    PublishRequest requestFor(const PlanReport& report, bool update = false) const;

private:
    void send(const PlanReport& report, bool update, std::function<void(const PublishResult&)> done);
    /// El nombre de siempre de un ciclo propio: requerimiento, revisión, plan, continuación, fecha y
    /// ambiente. Es el de los ciclos sueltos y el de los que se publicaron antes de los ciclos de fase.
    QString ownCycleName(const PlanReport& report) const;
    /// «GREQ 2026997 · Integración de servicios»: de qué son los Tests que se crean para el issue.
    QString testContextOf(const Issue& issue) const;

    /// Bugs reportados desde ese caso durante ese ciclo. Todos se enlazan a la ejecución en Zephyr;
    /// al paso, sólo los que salieron de la ejecución que se publica (`runId`): los de una repetición
    /// anterior del caso en el ciclo señalarían un paso que en ésta pudo pasar.
    QList<PublishDefect> defectsOf(const PlanReport& report, const QString& caseId, const QString& runId) const;
    /// Cuántas continuaciones lleva encadenadas el ciclo (0 = no es una continuación).
    int continuationDepth(const PlanRun& plan) const;

    std::shared_ptr<ITestManagement> m_zephyr;
    IssueStore* m_issues = nullptr;
    TestCaseStore& m_cases;
    RunHistoryStore& m_history;
    SettingsStore& m_settings;
    BugStore& m_bugs;
};

} // namespace qaflow
