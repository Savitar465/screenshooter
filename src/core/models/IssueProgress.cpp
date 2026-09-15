#include "IssueProgress.h"

#include <QCoreApplication>
#include <QHash>
#include <QSet>

#include <algorithm>

namespace qaflow {

namespace {

/// Momento por el que se ordena una ejecución: cuándo terminó, o cuándo empezó si sigue abierta.
QDateTime runTime(const RunRecord& run) { return run.finishedAt.isValid() ? run.finishedAt : run.startedAt; }

} // namespace

IssueProgress issueProgress(const Issue& issue, const QList<TestCase>& cases, const QList<RunRecord>& runs,
                            const QList<IssueLink>& bugs, const QDateTime& since) {
    IssueProgress p;

    QSet<QString> known;
    for (const auto& c : cases) known.insert(c.id);

    QSet<QString> linked;
    for (const auto& caseId : issue.caseIds) {
        if (caseId.isEmpty() || linked.contains(caseId)) continue;
        linked.insert(caseId);
        if (known.contains(caseId)) ++p.cases;
        else ++p.missingCases;
    }

    // De cada caso cuenta su ejecución más reciente dentro de la revisión: repetir un caso no suma.
    QHash<QString, RunRecord> latest;
    for (const auto& run : runs) {
        if (!linked.contains(run.caseId) || !known.contains(run.caseId)) continue;
        if (since.isValid() && runTime(run).isValid() && runTime(run) < since) continue;
        const auto it = latest.constFind(run.caseId);
        if (it != latest.constEnd() && runTime(*it) >= runTime(run)) continue;
        latest.insert(run.caseId, run);
    }
    for (const auto& run : latest) {
        ++p.executed;
        switch (run.verdict) {
            case Verdict::Superado: ++p.passed; break;
            case Verdict::Fallido: ++p.failed; break;
            case Verdict::Bloqueado: ++p.blocked; break;
        }
    }

    for (const auto& bug : bugs) {
        if (!linked.contains(bug.caseId)) continue;
        ++p.bugs;
        if (!bug.resolved) ++p.openBugs;
    }

    // El singular y el plural van escritos uno a uno para que se puedan traducir por separado.
    if (p.cases == 0) p.blockers << QCoreApplication::translate("core", "sin casos vinculados");
    if (p.notRun() == 1) p.blockers << QCoreApplication::translate("core", "%1 caso sin ejecutar").arg(p.notRun());
    else if (p.notRun() > 1) p.blockers << QCoreApplication::translate("core", "%1 casos sin ejecutar").arg(p.notRun());
    if (p.failed == 1) p.blockers << QCoreApplication::translate("core", "%1 caso fallido").arg(p.failed);
    else if (p.failed > 1) p.blockers << QCoreApplication::translate("core", "%1 casos fallidos").arg(p.failed);
    if (p.blocked == 1) p.blockers << QCoreApplication::translate("core", "%1 caso bloqueado").arg(p.blocked);
    else if (p.blocked > 1) p.blockers << QCoreApplication::translate("core", "%1 casos bloqueados").arg(p.blocked);
    if (p.openBugs == 1) p.blockers << QCoreApplication::translate("core", "%1 bug abierto").arg(p.openBugs);
    else if (p.openBugs > 1) p.blockers << QCoreApplication::translate("core", "%1 bugs abiertos").arg(p.openBugs);

    if (p.failed > 0 || p.blocked > 0 || p.openBugs > 0) p.suggested = QaOutcome::Observado;
    else if (p.executed > 0 && p.notRun() == 0) p.suggested = QaOutcome::Conforme;
    else p.suggested = QaOutcome::Pendiente;
    return p;
}

} // namespace qaflow
