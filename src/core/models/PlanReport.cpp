#include "PlanReport.h"

#include <QCoreApplication>
#include <QHash>

#include <algorithm>

namespace qaflow {

Verdict PlanReport::verdict() const {
    if (blocked > 0) return Verdict::Bloqueado;
    if (failed > 0) return Verdict::Fallido;
    return Verdict::Superado;
}

QStringList PlanReport::brokenCaseIds() const {
    QStringList out;
    for (const auto& row : rows)
        if (row.executed && row.run.isBroken()) out << row.caseId;
    return out;
}

QList<IssueLink> PlanReport::bugs() const {
    QList<IssueLink> out;
    for (const auto& row : rows) out += row.bugs;
    return out;
}

int PlanReport::bugCount() const {
    int n = 0;
    for (const auto& row : rows) n += int(row.bugs.size());
    return n;
}

int PlanReport::openBugCount() const {
    int n = 0;
    for (const auto& row : rows)
        n += int(std::count_if(row.bugs.cbegin(), row.bugs.cend(), [](const IssueLink& b) { return !b.resolved; }));
    return n;
}

QList<QPair<QString, int>> PlanReport::bugCountsByType() const {
    QList<QPair<QString, int>> out;
    for (const auto& row : rows)
        for (const auto& bug : row.bugs) {
            const QString type = bug.issueType.trimmed();
            const auto it = std::find_if(out.begin(), out.end(), [&type](const QPair<QString, int>& p) { return p.first == type; });
            if (it == out.end()) out.append({type, 1});
            else ++it->second;
        }
    return out;
}

bool PlanReport::foundIn(const PlanRun& plan, const IssueLink& bug) {
    // Lo que dice el propio bug manda: si salió de otro ciclo (o de una ejecución suelta), no es de éste
    // aunque coincidan las fechas.
    if (!bug.planRunId.trimmed().isEmpty()) return bug.planRunId == plan.id;
    if (!bug.runId.trimmed().isEmpty()) return false;
    return reportedDuring(plan, bug);
}

bool PlanReport::foundIn(const RunRecord& run, const IssueLink& bug) {
    if (!bug.runId.trimmed().isEmpty()) return bug.runId == run.id;
    if (bug.caseId != run.caseId) return false;
    if (!run.startedAt.isValid() || !bug.createdAt.isValid()) return false;
    const QDateTime until = run.finishedAt.isValid() ? run.finishedAt.addSecs(3600) : QDateTime::currentDateTime();
    return bug.createdAt >= run.startedAt && bug.createdAt <= until;
}

bool PlanReport::reportedDuring(const PlanRun& plan, const IssueLink& bug) {
    if (!plan.startedAt.isValid() || !bug.createdAt.isValid()) return true;   // sin fechas no se puede descartar
    const QDateTime until = plan.isFinished() ? plan.finishedAt.addSecs(3600) : QDateTime::currentDateTime();
    return bug.createdAt >= plan.startedAt && bug.createdAt <= until;
}

PlanReport PlanReport::build(const PlanRun& plan, const QList<RunRecord>& runsOfPlan, const CaseLookup& caseOf,
                             const QList<IssueLink>& bugs) {
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
        // La historia sale del caso de hoy; el Test, de la ejecución: es el que se creó al publicarla.
        const CaseInfo info = caseOf ? caseOf(caseId) : CaseInfo{};
        row.jiraKey = info.jiraKey;
        if (const auto it = latest.constFind(caseId); it != latest.cend()) {
            row.executed = true;
            row.run = **it;
            row.testKey = row.run.testKey;
            row.title = row.run.caseTitle;
            row.suite = row.run.suite;
            ++r.executed;
            switch (row.run.verdict) {
                case Verdict::Superado: ++r.passed; break;
                case Verdict::Fallido: ++r.failed; break;
                case Verdict::Bloqueado: ++r.blocked; break;
            }
        } else {
            row.title = info.title;
        }
        for (const auto& bug : bugs)
            if (bug.caseId == caseId && foundIn(plan, bug)) row.bugs.append(bug);
        std::sort(row.bugs.begin(), row.bugs.end(), [](const IssueLink& a, const IssueLink& b) { return a.createdAt > b.createdAt; });
        r.rows.append(row);
    }
    return r;
}

QString PlanReport::toMarkdown() const {
    QStringList out;
    out << QCoreApplication::translate("core", "# Informe de plan · %1").arg(plan.name);
    out << QString();
    out << QCoreApplication::translate("core", "- **Inicio:** %1").arg(plan.startedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
    if (plan.isFinished()) out << QCoreApplication::translate("core", "- **Fin:** %1").arg(plan.finishedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
    // Dónde quedaron estos resultados, para que el informe pegado en un ticket lo diga también.
    if (plan.isPublished())
        out << QCoreApplication::translate("core", "- **Ciclo de Zephyr:** %1 · publicado el %2")
                   .arg(plan.zephyrCycleId, plan.publishedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")));
    out << QCoreApplication::translate("core", "- **Veredicto:** %1").arg(label(verdict()));
    out << QCoreApplication::translate("core", "- **Casos:** %1 · Superados %2 · Fallidos %3 · Bloqueados %4 · Pendientes %5")
               .arg(total()).arg(passed).arg(failed).arg(blocked).arg(pending());
    out << QCoreApplication::translate("core", "- **Tasa de éxito:** %1 %").arg(successRate());
    if (bugCount() > 0) {
        QString line = QCoreApplication::translate("core", "- **Bugs encontrados:** %1 · %2 abiertos").arg(bugCount()).arg(openBugCount());
        // Errores y mejoras se cuentan por separado: son dos cosas distintas aunque se reporten igual.
        QStringList byType;
        for (const auto& [type, count] : bugCountsByType())
            byType << QStringLiteral("%1 %2").arg(count).arg(type.isEmpty() ? QCoreApplication::translate("core", "sin tipo") : type);
        if (!byType.isEmpty()) line += QStringLiteral(" · ") + byType.join(QStringLiteral(" · "));
        out << line;
    }
    out << QCoreApplication::translate("core", "- **Duración acumulada:** %1").arg(formatDuration(durationSecs));
    out << QString();
    out << QCoreApplication::translate("core", "| Caso | Título | Suite | Resultado | Pasos | Duración |");
    out << QStringLiteral("|------|--------|-------|-----------|-------|----------|");
    for (const auto& row : rows) {
        if (!row.executed) {
            out << QCoreApplication::translate("core", "| %1 | %2 |  | Pendiente |  |  |").arg(row.caseId, row.title);
            continue;
        }
        out << QStringLiteral("| %1 | %2 | %3 | %4 | %5/%6 | %7 |")
                   .arg(row.caseId, row.title, row.suite, label(row.run.verdict))
                   .arg(row.run.steps.size()).arg(row.run.plannedSteps)
                   .arg(formatDuration(row.run.durationSecs));
    }

    // Los bugs que salieron del ciclo, juntos: es lo primero que se mira al leer el informe en un ticket.
    if (bugCount() > 0) {
        out << QString();
        out << QCoreApplication::translate("core", "## Bugs encontrados · %1").arg(bugCount());
        out << QString();
        out << QCoreApplication::translate("core", "| Bug | Tipo | Caso | Paso | Título | Severidad | Estado |");
        out << QStringLiteral("|-----|------|------|------|--------|-----------|--------|");
        for (const auto& row : rows) {
            for (const auto& bug : row.bugs) {
                out << QStringLiteral("| %1 | %2 | %3 | %4 | %5 | %6 | %7 |")
                           .arg(bug.key,
                                bug.issueType.trimmed().isEmpty() ? QStringLiteral("—") : bug.issueType.trimmed(),
                                bug.caseId,
                                bug.step > 0 ? QString::number(bug.step) : QString(),
                                bug.title, BugReport::severityLabel(bug.severity),
                                bug.status.isEmpty() ? (bug.resolved ? QCoreApplication::translate("core", "Cerrado")
                                                                     : QCoreApplication::translate("core", "Abierto"))
                                                     : bug.status);
            }
        }
    }

    for (const auto& row : rows) {
        if (!row.executed) continue;
        out << QString();
        out << QStringLiteral("## %1 · %2 — %3").arg(row.caseId, row.title, label(row.run.verdict));
        QStringList links;
        if (!row.jiraKey.trimmed().isEmpty()) links << QCoreApplication::translate("core", "**Historia:** %1").arg(row.jiraKey.trimmed());
        if (!row.testKey.trimmed().isEmpty()) links << QCoreApplication::translate("core", "**Test:** %1").arg(row.testKey.trimmed());
        if (!row.bugs.isEmpty()) {
            QStringList keys;
            for (const auto& bug : row.bugs)
                keys << (bug.step > 0 ? QCoreApplication::translate("core", "%1 (paso %2)").arg(bug.key).arg(bug.step) : bug.key);
            links << QCoreApplication::translate("core", "**Bugs:** %1").arg(keys.join(QStringLiteral(", ")));
        }
        if (!links.isEmpty()) out << links.join(QStringLiteral(" · "));
        for (int i = 0; i < row.run.steps.size(); ++i) {
            const auto& s = row.run.steps[i];
            QString line = QStringLiteral("%1. [%2] %3").arg(i + 1).arg(label(s.result), s.action);
            if (!s.note.trimmed().isEmpty()) line += QStringLiteral(" — _%1_").arg(s.note.trimmed());
            out << line;
        }
    }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

} // namespace qaflow
