#pragma once

#include "core/models/IssueLink.h"
#include "core/models/RunHistory.h"

#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <functional>

namespace qaflow {

/// Una fila del informe: un caso del plan, ejecutado o pendiente, con los enlaces del caso.
struct PlanReportRow {
    QString caseId;
    QString title;
    QString suite;
    QString jiraKey;          // historia de Jira enlazada al caso
    QString testKey;          // Test de Zephyr creado para esta ejecución al publicar el informe (vacío si no se publicó)
    bool executed = false;
    RunRecord run;            // válido sólo si executed (la última ejecución de ese caso dentro del plan)
    /// Bugs de ese caso reportados mientras corría el ciclo, del más reciente al primero. Vacío si el
    /// informe se construyó sin el libro de bugs.
    QList<IssueLink> bugs;
};

/// Informe de una ejecución de plan. Se calcula a partir del historial; no se persiste.
struct PlanReport {
    PlanRun plan;
    QList<PlanReportRow> rows;    // en el orden del plan

    int total() const { return rows.size(); }
    int executed = 0;
    int passed = 0;
    int failed = 0;
    int blocked = 0;
    int pending() const { return total() - executed; }
    qint64 durationSecs = 0;      // suma de las ejecuciones

    /// Porcentaje de superados sobre ejecutados (0 si no se ejecutó nada).
    int successRate() const { return executed ? passed * 100 / executed : 0; }
    Verdict verdict() const;

    /// Casos que quedaron rotos: los ejecutados con veredicto fallado o bloqueado, en el orden del
    /// plan. Son los que repite una continuación del ciclo.
    QStringList brokenCaseIds() const;
    /// El ciclo se puede continuar: terminó y dejó algún caso roto.
    bool canContinue() const { return plan.isFinished() && !brokenCaseIds().isEmpty(); }

    /// Los bugs del ciclo, caso por caso y en el orden del plan.
    QList<IssueLink> bugs() const;
    int bugCount() const;
    /// Los que el gestor todavía no da por cerrados.
    int openBugCount() const;
    /// Cuántos hallazgos de cada tipo del gestor trae el ciclo («Bug», «Improvement»…), en el orden
    /// en que aparecen. QAflow reporta las dos cosas —lo que está mal y lo que se pide cambiar—, así
    /// que el informe las cuenta por separado en vez de llamarlas a todas «bugs». Los guardados sin
    /// tipo van juntos, con la clave vacía.
    QList<QPair<QString, int>> bugCountsByType() const;

    /// Si ese bug salió de ese ciclo. Los bugs se reportan desde una ejecución y anotan de cuál
    /// (`IssueLink::planRunId`), así que es una respuesta exacta; los anteriores a que se anotara, y
    /// los traídos del gestor, se atribuyen por fecha (`reportedDuring`). No mira de qué caso es: eso
    /// lo decide quien llama.
    static bool foundIn(const PlanRun& plan, const IssueLink& bug);
    /// Si ese bug salió de esa ejecución concreta del caso (`IssueLink::runId`). Los antiguos, por
    /// caso y por la ventana de la ejecución.
    static bool foundIn(const RunRecord& run, const IssueLink& bug);
    /// Si ese bug se reportó mientras corría el ciclo. El margen de una hora tras el cierre es porque
    /// el parte se escribe justo después de ver el fallo, cuando la ejecución ya se ha archivado.
    /// Es la regla de los bugs que no dicen de qué ejecución salieron.
    static bool reportedDuring(const PlanRun& plan, const IssueLink& bug);

    /// Lo que el informe necesita del catálogo: el título de los casos pendientes (no hay RunRecord
    /// del que sacarlo) y la historia enlazada al caso, que es la de ahora y no la de aquel día.
    struct CaseInfo {
        QString title;
        QString jiraKey;
    };
    using CaseLookup = std::function<CaseInfo(const QString& caseId)>;
    /// `bugs` es el libro de bugs del proyecto entero: se queda con los que salieron de este ciclo
    /// (`foundIn`), en la fila del caso desde el que se reportaron. Sin él, el informe sale sin bugs.
    static PlanReport build(const PlanRun& plan, const QList<RunRecord>& runsOfPlan, const CaseLookup& caseOf = {},
                            const QList<IssueLink>& bugs = {});

    QString toMarkdown() const;
};

} // namespace qaflow
