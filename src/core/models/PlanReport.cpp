#include "PlanReport.h"

#include <QHash>

namespace qaflow {

Verdict PlanReport::verdict() const {
    if (blocked > 0) return Verdict::Bloqueado;
    if (failed > 0) return Verdict::Fallido;
    return Verdict::Superado;
}

PlanReport PlanReport::build(const PlanRun& plan, const QList<RunRecord>& runsOfPlan, const TitleLookup& titleOf) {
    PlanReport r;
    r.plan = plan;

    // Si un caso se repitió dentro del plan cuenta la última ejecución.
    QHash<QString, const RunRecord*> latest;
    for (const auto& run : runsOfPlan) {
        if (run.planRunId != plan.id) continue;
        const auto it = latest.constFind(run.caseId);
        if (it == latest.cend() || (*it)->finishedAt < run.finishedAt) latest[run.caseId] = &run;
        r.durationSecs += run.durationSecs;
    }

    for (const auto& caseId : plan.caseIds) {
        PlanReportRow row;
        row.caseId = caseId;
        if (const auto it = latest.constFind(caseId); it != latest.cend()) {
            row.executed = true;
            row.run = **it;
            row.title = row.run.caseTitle;
            row.suite = row.run.suite;
            ++r.executed;
            switch (row.run.verdict) {
                case Verdict::Superado: ++r.passed; break;
                case Verdict::Fallido: ++r.failed; break;
                case Verdict::Bloqueado: ++r.blocked; break;
            }
        } else if (titleOf) {
            row.title = titleOf(caseId);
        }
        r.rows.append(row);
    }
    return r;
}

QString PlanReport::toMarkdown() const {
    QStringList out;
    out << QStringLiteral("# Informe de plan · %1").arg(plan.name);
    out << QString();
    out << QStringLiteral("- **Inicio:** %1").arg(plan.startedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
    if (plan.isFinished()) out << QStringLiteral("- **Fin:** %1").arg(plan.finishedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
    out << QStringLiteral("- **Veredicto:** %1").arg(toString(verdict()));
    out << QStringLiteral("- **Casos:** %1 · Superados %2 · Fallidos %3 · Bloqueados %4 · Pendientes %5")
               .arg(total()).arg(passed).arg(failed).arg(blocked).arg(pending());
    out << QStringLiteral("- **Tasa de éxito:** %1 %").arg(successRate());
    out << QStringLiteral("- **Duración acumulada:** %1").arg(formatDuration(durationSecs));
    out << QString();
    out << QStringLiteral("| Caso | Título | Suite | Resultado | Pasos | Duración |");
    out << QStringLiteral("|------|--------|-------|-----------|-------|----------|");
    for (const auto& row : rows) {
        if (!row.executed) {
            out << QStringLiteral("| %1 | %2 |  | Pendiente |  |  |").arg(row.caseId, row.title);
            continue;
        }
        out << QStringLiteral("| %1 | %2 | %3 | %4 | %5/%6 | %7 |")
                   .arg(row.caseId, row.title, row.suite, toString(row.run.verdict))
                   .arg(row.run.steps.size()).arg(row.run.plannedSteps)
                   .arg(formatDuration(row.run.durationSecs));
    }

    for (const auto& row : rows) {
        if (!row.executed) continue;
        out << QString();
        out << QStringLiteral("## %1 · %2 — %3").arg(row.caseId, row.title, toString(row.run.verdict));
        for (int i = 0; i < row.run.steps.size(); ++i) {
            const auto& s = row.run.steps[i];
            QString line = QStringLiteral("%1. [%2] %3").arg(i + 1).arg(toString(s.result), s.action);
            if (!s.note.trimmed().isEmpty()) line += QStringLiteral(" — _%1_").arg(s.note.trimmed());
            out << line;
        }
    }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace qaflow
