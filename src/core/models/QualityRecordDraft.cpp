#include "QualityRecordDraft.h"

#include "core/models/BugReport.h"

#include <QCoreApplication>
#include <QDate>
#include <QHash>

#include <algorithm>

namespace qaflow::quality {

namespace {

/// El primer valor no vacío: lo que trae el requerimiento, lo que ya había en el acta anterior o el
/// texto con el que el formulario dice «no aplica» / «sin dato».
QString firstOf(const QStringList& candidates) {
    for (const auto& candidate : candidates)
        if (!candidate.trimmed().isEmpty()) return candidate.trimmed();
    return {};
}

/// Busca en la ficha un campo cuya etiqueta contenga alguna de esas palabras (sin distinguir
/// mayúsculas): GESREQ no siempre escribe la etiqueta igual y así no se depende de una exacta.
QString detailFieldLike(const RequirementDetail& detail, const QStringList& words) {
    auto matches = [&words](const QString& label) {
        return std::any_of(words.cbegin(), words.cend(), [&label](const QString& word) {
            return label.contains(word, Qt::CaseInsensitive);
        });
    };
    for (const auto& field : detail.fields)
        if (matches(field.label) && !field.value.trimmed().isEmpty()) return field.value.trimmed();
    for (const auto& section : detail.sections)
        for (const auto& field : section.fields)
            if (matches(field.label) && !field.value.trimmed().isEmpty()) return field.value.trimmed();
    return {};
}

/// La última ejecución de cada caso, por id de caso. Con `onlyCases`, sólo la de esos casos.
QHash<QString, RunRecord> latestRuns(const QList<RunRecord>& runs, const QStringList& onlyCases = {}) {
    QHash<QString, RunRecord> latest;
    for (const auto& run : runs) {
        if (!onlyCases.isEmpty() && !onlyCases.contains(run.caseId)) continue;
        const QDateTime when = run.finishedAt.isValid() ? run.finishedAt : run.startedAt;
        const auto it = latest.constFind(run.caseId);
        if (it != latest.constEnd()) {
            const QDateTime other = it->finishedAt.isValid() ? it->finishedAt : it->startedAt;
            if (other >= when) continue;
        }
        latest.insert(run.caseId, run);
    }
    return latest;
}

} // namespace

QualityRecord draftFor(const Issue& issue, const QList<TestCase>& cases, const QList<RunRecord>& runs,
                       const QList<IssueLink>& bugs, const DraftContext& context) {
    const QualityRecord& previous = context.previous;
    const ExternalRequirement& requirement = issue.requirement.data;
    const RequirementDetail& detail = issue.requirement.detail;

    QualityRecord record;
    record.greq = requirement.id;
    record.system = firstOf({requirement.system, requirement.systemCode, detail.systemCode, previous.system});
    record.description = firstOf({detail.description, requirement.summary, issue.title});
    record.developedBy = firstOf({detailFieldLike(detail, {QStringLiteral("desarroll"), QStringLiteral("asignado")}),
                                  previous.developedBy});
    record.qaResource = firstOf({context.qaResource, previous.qaResource, requirement.user});
    record.revisionNumber = context.revisionNumber;

    // Lo que no está en GESREQ se hereda del acta anterior del proyecto; si no hay, queda como lo
    // escribe el formulario.
    record.moduleLink = previous.moduleLink;
    record.server = firstOf({previous.server, record.server});
    record.dbAccess = firstOf({previous.dbAccess, record.dbAccess});
    record.dbSchema = firstOf({previous.dbSchema, record.dbSchema});
    record.dbUser = firstOf({previous.dbUser, record.dbUser});
    record.appUser = firstOf({previous.appUser, record.appUser});
    record.tables = firstOf({previous.tables, record.tables});
    record.functions = firstOf({previous.functions, record.functions});
    record.department = firstOf({previous.department, record.department});
    record.logoPath = previous.logoPath;

    // Fechas de revisión: de la primera a la última ejecución de la ronda.
    for (const auto& run : runs) {
        const QDate start = run.startedAt.date();
        const QDate end = (run.finishedAt.isValid() ? run.finishedAt : run.startedAt).date();
        if (start.isValid() && (!record.from.isValid() || start < record.from)) record.from = start;
        if (end.isValid() && (!record.to.isValid() || end > record.to)) record.to = end;
    }

    // Resumen de observaciones: los bugs de esta ronda por tipo, y como correcciones los de las
    // anteriores que ya están cerrados.
    for (auto& observation : record.observations) {
        observation.observations = int(std::count_if(bugs.cbegin(), bugs.cend(), [&observation](const IssueLink& bug) {
            return bug.classification == observation.type;
        }));
        observation.corrections = int(std::count_if(context.previousBugs.cbegin(), context.previousBugs.cend(),
                                                    [&observation](const IssueLink& bug) {
                                                        return bug.classification == observation.type && bug.resolved;
                                                    }));
    }

    // Detalles de la revisión: dónde están los casos, dónde la ejecución y qué bugs salieron.
    const QHash<QString, RunRecord> latest = latestRuns(runs, issue.caseIds);
    QStringList design;
    if (issue.isPublished()) {
        design << QCoreApplication::translate("core", "Jira: %1").arg(issue.publication.publishedTitle.isEmpty() ? issue.title : issue.publication.publishedTitle);
        if (!issue.publication.url.isEmpty()) design << QCoreApplication::translate("core", "Enlace: %1").arg(issue.publication.url);
    }
    for (const auto& id : issue.caseIds) {
        const auto it = std::find_if(cases.cbegin(), cases.cend(), [&id](const TestCase& c) { return c.id == id; });
        if (it == cases.cend()) continue;
        const QString key = firstOf({latest.value(id).testKey, it->jiraKey, it->id});
        design << QCoreApplication::translate("core", "%1 — %2 — %3 paso(s)").arg(key, it->title).arg(it->steps.size());
    }
    record.caseDesign = design.join(QLatin1Char('\n'));

    QStringList execution;
    if (!issue.publication.url.isEmpty()) execution << issue.publication.url;
    int passed = 0, failed = 0, blocked = 0;
    for (const auto& run : latest) {
        switch (run.verdict) {
            case Verdict::Superado: ++passed; break;
            case Verdict::Fallido: ++failed; break;
            case Verdict::Bloqueado: ++blocked; break;
        }
    }
    execution << QCoreApplication::translate("core", "%1 caso(s) ejecutado(s): %2 superado(s), %3 fallido(s), %4 bloqueado(s)")
                     .arg(latest.size())
                     .arg(passed)
                     .arg(failed)
                     .arg(blocked);
    record.execution = execution.join(QLatin1Char('\n'));

    QStringList reported;
    for (const auto& bug : bugs)
        reported << (bug.url.isEmpty() ? bug.key : bug.url) + (bug.title.isEmpty() ? QString() : QStringLiteral(" — ") + bug.title);
    record.bugs = reported.join(QLatin1Char('\n'));
    return record;
}

QString summaryOf(const QualityRecord& record, QaOutcome outcome, const QList<RunRecord>& runs) {
    // Como en el acta: de cada caso cuenta su última ejecución, no cuántas veces se repitió.
    const QHash<QString, RunRecord> latest = latestRuns(runs);
    int passed = 0, failed = 0, blocked = 0;
    for (const auto& run : latest) {
        switch (run.verdict) {
            case Verdict::Superado: ++passed; break;
            case Verdict::Fallido: ++failed; break;
            case Verdict::Bloqueado: ++blocked; break;
        }
    }

    QStringList out;
    out << QCoreApplication::translate("core", "Control de calidad GREQ %1 — revisión %2: %3").arg(record.greq).arg(record.revisionNumber).arg(label(outcome));
    if (!record.reviewDates().isEmpty()) out << QCoreApplication::translate("core", "Fecha de revisión: %1").arg(record.reviewDates());
    out << QCoreApplication::translate("core", "Casos ejecutados: %1 (%2 superados, %3 fallidos, %4 bloqueados)")
               .arg(latest.size())
               .arg(passed)
               .arg(failed)
               .arg(blocked);
    if (record.totalObservations() > 0) {
        out << QCoreApplication::translate("core", "Observaciones: %1").arg(record.totalObservations());
        for (const auto& observation : record.observations)
            if (observation.observations > 0)
                out << QStringLiteral("- %1 · %2: %3")
                           .arg(observation.type, BugReport::classificationName(observation.type))
                           .arg(observation.observations);
    } else {
        out << QCoreApplication::translate("core", "Sin observaciones");
    }
    if (!record.generalNotes.trimmed().isEmpty()) out << QCoreApplication::translate("core", "Observaciones generales: %1").arg(record.generalNotes.trimmed());
    return out.join(QLatin1Char('\n'));
}

} // namespace qaflow::quality
