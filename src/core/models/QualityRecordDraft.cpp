#include "QualityRecordDraft.h"

#include "core/models/BugReport.h"

#include <QCoreApplication>
#include <QDate>
#include <QHash>
#include <QRegularExpression>
#include <QSet>

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

/// La primera dirección web que aparece en un texto. De un enlace a una rama o a un merge request se
/// queda con el repositorio, que es lo que el acta llama «Enlace/módulo».
QString repositoryIn(const QString& text) {
    static const QRegularExpression url(QStringLiteral("https?://\\S+"));
    const QRegularExpressionMatch match = url.match(text);
    if (!match.hasMatch()) return {};
    QString link = match.captured(0);
    while (!link.isEmpty() && QStringLiteral(".,;:)»\"'").contains(link.back())) link.chop(1);
    for (const auto& cut : {QStringLiteral("/-/merge_requests"), QStringLiteral("/-/tree"), QStringLiteral("/pull/")}) {
        const int at = link.indexOf(cut);
        if (at > 0) return link.left(at);
    }
    return link;
}

QString day(const QDateTime& when) { return when.toString(QStringLiteral("dd/MM/yyyy")); }

/// Cómo quedó un caso del ciclo: "TC-104 Checkout — Superado (4/4 pasos)".
QString executionLine(const PlanReportRow& row) {
    if (!row.executed)
        return QCoreApplication::translate("core", "%1 %2 — sin ejecutar").arg(row.caseId, row.title);
    const RunRecord& run = row.run;
    const int steps = run.steps.size();
    const int planned = run.plannedSteps > 0 ? run.plannedSteps : steps;
    QString line = QCoreApplication::translate("core", "%1 %2 — %3 (%4 de %5 pasos)")
                       .arg(row.caseId, row.title, label(run.verdict))
                       .arg(steps)
                       .arg(planned);
    if (run.durationSecs > 0) line += QStringLiteral(" · ") + formatDuration(run.durationSecs);
    return line;
}

/// Recuento de los ciclos por veredicto, contando una vez cada caso (su ejecución más reciente).
struct Counts {
    int cases = 0;
    int executed = 0;
    int passed = 0;
    int failed = 0;
    int blocked = 0;
};

Counts countOf(const QList<PlanReport>& cycles) {
    Counts c;
    QHash<QString, Verdict> latest;
    QSet<QString> seen;
    for (const auto& cycle : cycles)
        for (const auto& row : cycle.rows) {
            if (!seen.contains(row.caseId)) {
                seen.insert(row.caseId);
                ++c.cases;
            }
            if (row.executed) latest.insert(row.caseId, row.run.verdict);
        }
    for (const auto verdict : latest) {
        ++c.executed;
        switch (verdict) {
            case Verdict::Superado: ++c.passed; break;
            case Verdict::Fallido: ++c.failed; break;
            case Verdict::Bloqueado: ++c.blocked; break;
        }
    }
    return c;
}

} // namespace

QualityRecord draftFor(const Issue& issue, const QList<TestCase>& cases, const QList<PlanReport>& cycles,
                       const QList<IssueLink>& bugs, const DraftContext& context) {
    const QualityRecord& previous = context.previous;
    const ExternalRequirement& requirement = issue.requirement.data;
    const RequirementDetail& detail = issue.requirement.detail;

    QualityRecord record;
    record.greq = requirement.id;
    record.process = firstOf({previous.process, record.process});
    record.system = firstOf({requirement.system, requirement.systemCode, detail.systemCode, previous.system});
    record.revisionNumber = context.revisionNumber;
    record.qaResource = firstOf({context.qaResource, previous.qaResource, requirement.user});
    record.developedBy = firstOf({detailFieldLike(detail, {QStringLiteral("desarroll"), QStringLiteral("analista"),
                                                           QStringLiteral("programador"), QStringLiteral("asignado")}),
                                  previous.developedBy});

    // El alcance del requerimiento, con los datos de la ficha que lo sitúan (quién lo pide, con qué
    // prioridad y en qué estado está): en el acta es el único sitio donde caben.
    QStringList description{firstOf({detail.description, requirement.summary, issue.title})};
    QStringList about;
    const QString requester = firstOf({detail.requester, requirement.requester});
    const QString unit = firstOf({detail.requestingUnit, requirement.requestingUnit});
    if (!requester.isEmpty())
        about << QCoreApplication::translate("core", "Solicitado por: %1").arg(unit.isEmpty() ? requester
                                                                                             : QStringLiteral("%1 (%2)").arg(requester, unit));
    if (!detail.requestType.trimmed().isEmpty()) about << QCoreApplication::translate("core", "Tipo: %1").arg(detail.requestType.trimmed());
    if (!detail.reference.trimmed().isEmpty()) about << QCoreApplication::translate("core", "Referencia: %1").arg(detail.reference.trimmed());
    const QString priority = firstOf({detail.priority, requirement.priority});
    if (!priority.isEmpty()) about << QCoreApplication::translate("core", "Prioridad: %1").arg(priority);
    const QString state = firstOf({detail.state, requirement.states.join(QStringLiteral(" + "))});
    if (!state.isEmpty()) about << QCoreApplication::translate("core", "Estado en GESREQ: %1").arg(state);
    if (!about.isEmpty()) description << QString() << about.join(QStringLiteral(" · "));
    record.description = description.join(QLatin1Char('\n')).trimmed();

    // Lo que la ficha de GESREQ sí dice del entorno; lo que no, se hereda del acta anterior del
    // proyecto y, si tampoco hay, queda como lo escribe el formulario.
    record.moduleLink = firstOf({detailFieldLike(detail, {QStringLiteral("enlace"), QStringLiteral("módulo"), QStringLiteral("modulo"),
                                                          QStringLiteral("repositorio"), QStringLiteral("url")}),
                                 repositoryIn(detail.description), previous.moduleLink});
    record.server = firstOf({detailFieldLike(detail, {QStringLiteral("servidor")}), previous.server, record.server});
    record.dbAccess = firstOf({detailFieldLike(detail, {QStringLiteral("acceso a la bd"), QStringLiteral("acceso bd")}),
                               previous.dbAccess, record.dbAccess});
    record.dbSchema = firstOf({detailFieldLike(detail, {QStringLiteral("esquema")}), previous.dbSchema, record.dbSchema});
    record.dbUser = firstOf({detailFieldLike(detail, {QStringLiteral("usuario de bd"), QStringLiteral("usuario bd")}),
                             previous.dbUser, record.dbUser});
    record.appUser = firstOf({detailFieldLike(detail, {QStringLiteral("usuario aplicaci"), QStringLiteral("usuario de la aplicaci")}),
                              previous.appUser, record.appUser});
    record.tables = firstOf({detailFieldLike(detail, {QStringLiteral("tabla")}), previous.tables, record.tables});
    record.functions = firstOf({detailFieldLike(detail, {QStringLiteral("funci"), QStringLiteral("procedimiento")}),
                                previous.functions, record.functions});
    record.department = firstOf({previous.department, record.department});
    record.logoPath = previous.logoPath;

    // Fechas de revisión: del comienzo del primer ciclo al final del último.
    for (const auto& cycle : cycles) {
        const QDate start = cycle.plan.startedAt.date();
        const QDate end = (cycle.plan.isFinished() ? cycle.plan.finishedAt : cycle.plan.startedAt).date();
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

    // «Elaboración de Casos de prueba»: dónde está el trabajo en el gestor y qué casos lo componen,
    // con el Test de Zephyr de cada uno si sus resultados ya se publicaron.
    QStringList design;
    if (issue.isPublished()) {
        design << QCoreApplication::translate("core", "Jira: %1")
                      .arg(issue.publication.publishedTitle.isEmpty() ? issue.title : issue.publication.publishedTitle);
        if (!issue.publication.url.isEmpty()) design << QCoreApplication::translate("core", "Enlace: %1").arg(issue.publication.url);
    }
    QSet<QString> listed;
    for (const auto& cycle : cycles) {
        if (cycles.size() > 1 || !cycle.plan.name.trimmed().isEmpty())
            design << QCoreApplication::translate("core", "Plan «%1» · %2 caso(s)").arg(cycle.plan.name).arg(cycle.total());
        for (const auto& row : cycle.rows) {
            if (listed.contains(row.caseId)) continue;
            listed.insert(row.caseId);
            const auto it = std::find_if(cases.cbegin(), cases.cend(), [&row](const TestCase& c) { return c.id == row.caseId; });
            const int steps = it != cases.cend() ? int(it->steps.size()) : int(row.run.plannedSteps);
            const QString key = firstOf({row.testKey, row.jiraKey, row.caseId});
            design << QCoreApplication::translate("core", "%1 — %2 — %3 paso(s)").arg(key, row.title).arg(steps);
        }
    }
    record.caseDesign = design.join(QLatin1Char('\n'));

    // «Ejecución de casos de pruebas»: el ciclo que se ejecutó, dónde quedaron sus resultados y cómo
    // terminó cada caso.
    QStringList execution;
    if (!issue.publication.url.isEmpty()) execution << issue.publication.url;
    for (const auto& cycle : cycles) {
        const QString when = cycle.plan.isFinished() && cycle.plan.finishedAt.date() != cycle.plan.startedAt.date()
                                 ? QCoreApplication::translate("core", "%1 a %2").arg(day(cycle.plan.startedAt), day(cycle.plan.finishedAt))
                                 : day(cycle.plan.startedAt);
        execution << QCoreApplication::translate("core", "%1 · ciclo del %2 · %3 de %4 ejecutados: %5 superado(s), %6 fallido(s), %7 bloqueado(s)")
                         .arg(cycle.plan.name, when)
                         .arg(cycle.executed)
                         .arg(cycle.total())
                         .arg(cycle.passed)
                         .arg(cycle.failed)
                         .arg(cycle.blocked);
        const QString url = context.cycleUrls.value(cycle.plan.id);
        if (!url.isEmpty()) execution << QCoreApplication::translate("core", "Ciclo en Zephyr: %1").arg(url);
        for (const auto& row : cycle.rows) execution << executionLine(row);
    }
    record.execution = execution.join(QLatin1Char('\n'));

    QStringList reported;
    for (const auto& bug : bugs) {
        QString line = bug.url.isEmpty() ? bug.key : bug.url;
        if (!bug.title.isEmpty()) line += QStringLiteral(" — ") + bug.title;
        if (!bug.classification.isEmpty())
            line += QStringLiteral(" [%1 · %2]").arg(bug.classification, BugReport::classificationName(bug.classification));
        reported << line;
    }
    record.bugs = reported.join(QLatin1Char('\n'));
    return record;
}

QString summaryOf(const QualityRecord& record, QaOutcome outcome, const QList<PlanReport>& cycles, const DraftContext& context) {
    const Counts counts = countOf(cycles);

    QStringList out;
    out << QCoreApplication::translate("core", "Control de calidad GREQ %1 — revisión %2: %3")
               .arg(record.greq)
               .arg(record.revisionNumber)
               .arg(label(outcome));
    if (!record.reviewDates().isEmpty()) out << QCoreApplication::translate("core", "Fecha de revisión: %1").arg(record.reviewDates());
    out << QCoreApplication::translate("core", "Casos ejecutados: %1 de %2 (%3 superados, %4 fallidos, %5 bloqueados)")
               .arg(counts.executed)
               .arg(counts.cases)
               .arg(counts.passed)
               .arg(counts.failed)
               .arg(counts.blocked);
    for (const auto& cycle : cycles) {
        const QString url = context.cycleUrls.value(cycle.plan.id);
        out << (url.isEmpty() ? QCoreApplication::translate("core", "Plan «%1» · ciclo del %2")
                                    .arg(cycle.plan.name, day(cycle.plan.startedAt))
                              : QCoreApplication::translate("core", "Plan «%1» · ciclo del %2 · %3")
                                    .arg(cycle.plan.name, day(cycle.plan.startedAt), url));
    }
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
    if (!record.generalNotes.trimmed().isEmpty())
        out << QCoreApplication::translate("core", "Observaciones generales: %1").arg(record.generalNotes.trimmed());
    return out.join(QLatin1Char('\n'));
}

} // namespace qaflow::quality
