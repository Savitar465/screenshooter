#pragma once

#include "core/models/IssueLink.h"
#include "core/models/RunHistory.h"

#include <QList>
#include <QString>
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

    /// Los bugs del ciclo, caso por caso y en el orden del plan.
    QList<IssueLink> bugs() const;
    int bugCount() const;
    /// Los que el gestor todavía no da por cerrados.
    int openBugCount() const;

    /// Si ese bug se reportó mientras corría el ciclo. El margen de una hora tras el cierre es porque
    /// el parte se escribe justo después de ver el fallo, cuando la ejecución ya se ha archivado.
    /// No mira de qué caso es: eso lo decide quien llama.
    static bool reportedDuring(const PlanRun& plan, const IssueLink& bug);

    /// Lo que el informe necesita del catálogo: el título de los casos pendientes (no hay RunRecord
    /// del que sacarlo) y la historia enlazada al caso, que es la de ahora y no la de aquel día.
    struct CaseInfo {
        QString title;
        QString jiraKey;
    };
    using CaseLookup = std::function<CaseInfo(const QString& caseId)>;
    /// `bugs` es el libro de bugs del proyecto entero: se queda con los de los casos del ciclo
    /// reportados mientras corría (`reportedDuring`). Sin él, el informe sale sin bugs.
    static PlanReport build(const PlanRun& plan, const QList<RunRecord>& runsOfPlan, const CaseLookup& caseOf = {},
                            const QList<IssueLink>& bugs = {});

    QString toMarkdown() const;
};

} // namespace qaflow
