#include "QualityRecordService.h"

#include "application/BugStore.h"
#include "application/IssueStore.h"
#include "application/PlanStore.h"
#include "application/RunHistoryStore.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"
#include "application/TestPublishService.h"

#include <QSet>

#include <algorithm>

namespace qaflow {

QualityRecordService::QualityRecordService(IssueStore& issues, TestCaseStore& cases, PlanStore& plans, RunHistoryStore& history,
                                           BugStore& bugs, SettingsStore& settings, std::shared_ptr<IQualityRecordWriter> writer,
                                           TestPublishService* publish, QObject* parent)
    : QObject(parent), m_issues(issues), m_cases(cases), m_plans(plans), m_history(history), m_bugs(bugs), m_settings(settings),
      m_writer(std::move(writer)), m_publish(publish) {}

QDateTime QualityRecordService::revisionStart(const Issue& issue, int revision) const {
    const IssueRevision* round = issue.revision(revision);
    return round ? round->startedAt : QDateTime();
}

QDateTime QualityRecordService::revisionEnd(const Issue& issue, int revision) const {
    const IssueRevision* round = issue.revision(revision);
    return round ? round->closedAt : QDateTime();
}

QStringList QualityRecordService::caseIdsOf(const Issue& issue) const { return IssueStore::caseIdsOf(issue, m_plans); }

int QualityRecordService::revisionNumber(const Issue& issue, int revision) const {
    const IssueRevision* round = issue.revision(revision);
    return round ? round->number : 0;
}

QList<RunRecord> QualityRecordService::revisionRuns(const Issue& issue, int revision) const {
    return IssueStore::runsOfRevision(issue, m_history, revisionNumber(issue, revision));
}

QList<IssueLink> QualityRecordService::revisionBugs(const Issue& issue, int revision) const {
    // Lo que se encontró probando esta ronda: los bugs salen de una ejecución y anotan de qué ciclo,
    // así que son los de los ciclos de la ronda.
    QSet<QString> cycleIds;
    for (const auto& cycle : IssueStore::cyclesOfRevision(issue, m_history, revisionNumber(issue, revision)))
        cycleIds.insert(cycle.id);
    // Los que no lo anotaron (los anteriores, y los reportados fuera de un ciclo) se sitúan como se
    // hacía entonces: por caso del issue y por la ventana de la ronda, de cuándo se abrió a cuándo se
    // cerró. La que sigue abierta no tiene final: cuenta todo lo reportado desde que empezó.
    const QDateTime since = revisionStart(issue, revision);
    const QDateTime until = revisionEnd(issue, revision);
    const QStringList caseIds = caseIdsOf(issue);
    QList<IssueLink> out;
    for (const auto& bug : m_bugs.issues()) {
        if (!bug.planRunId.trimmed().isEmpty()) {
            if (cycleIds.contains(bug.planRunId)) out << bug;
            continue;
        }
        if (!caseIds.contains(bug.caseId)) continue;
        if (since.isValid() && bug.createdAt.isValid() && bug.createdAt < since) continue;
        if (until.isValid() && bug.createdAt.isValid() && bug.createdAt > until) continue;
        out << bug;
    }
    return out;
}

IssueProgress QualityRecordService::progressFor(const QString& issueId) const {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) return {};
    return issueProgress(caseIdsOf(*issue), m_cases.cases(), revisionRuns(*issue), m_bugs.issues(), revisionStart(*issue));
}

QList<PlanReport> QualityRecordService::cyclesFor(const QString& issueId, int revision) const {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) return {};
    const int number = revisionNumber(*issue, revision);
    QList<PlanRun> cycles = IssueStore::cyclesOfRevision(*issue, m_history, number);
    // Una revisión abierta a mano (o abierta después de ejecutar) puede no tener ciclos suyos: entonces
    // se ofrecen todos los del issue en vez de dejar el acta sin ejecución de la que hablar. Sólo vale
    // para la ronda en curso: una ronda anterior se publica con lo que se probó en ella, y coger los
    // ciclos de otra ronda los mandaría dos veces a Zephyr y al comentario.
    if (cycles.isEmpty() && number == issue->currentRevisionNumber()) cycles = IssueStore::cyclesOf(*issue, m_history);
    QList<PlanReport> reports;
    for (const auto& cycle : cycles) reports << m_history.report(cycle.id);
    return reports;
}

QString QualityRecordService::recordCycleFor(const QString& issueId, int revision) const {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) return {};
    const QList<PlanReport> cycles = cyclesFor(issueId, revision);
    const IssueRevision* round = issue->revision(revision);
    const QString saved = round ? round->planRunId : QString();
    if (!saved.isEmpty() &&
        std::any_of(cycles.cbegin(), cycles.cend(), [&saved](const PlanReport& r) { return r.plan.id == saved; }))
        return saved;
    return cycles.isEmpty() ? QString() : cycles.first().plan.id;
}

QList<PlanReport> QualityRecordService::cyclesForRecord(const Issue& issue, const QString& planRunId, int revision) const {
    const QList<PlanReport> cycles = cyclesFor(issue.id, revision);
    if (planRunId.trimmed().isEmpty()) return cycles;
    for (const auto& cycle : cycles)
        if (cycle.plan.id == planRunId) return {cycle};
    return cycles;
}

quality::DraftContext QualityRecordService::contextFor(const Issue& issue, const QList<PlanReport>& cycles, int revision) const {
    quality::DraftContext context;
    context.qaResource = m_settings.requirementSource().user;
    const int number = revisionNumber(issue, revision);
    context.revisionNumber = number > 0 ? number : 1;
    context.previous = previousRecord(issue.id);

    const QDateTime since = revisionStart(issue, revision);
    const QStringList caseIds = caseIdsOf(issue);
    for (const auto& bug : m_bugs.issues())
        if (caseIds.contains(bug.caseId) && since.isValid() && bug.createdAt.isValid() && bug.createdAt < since)
            context.previousBugs << bug;

    if (m_publish)
        for (const auto& cycle : cycles) {
            const QString url = m_publish->cycleUrl(cycle);
            if (!url.isEmpty()) context.cycleUrls.insert(cycle.plan.id, url);
        }
    return context;
}

QualityRecord QualityRecordService::previousRecord(const QString& issueId) const {
    QualityRecord previous;
    QDateTime newest;
    for (const auto& issue : m_issues.issues())
        for (const auto& revision : issue.revisions) {
            // Del mismo issue vale cualquier acta anterior; de los demás, sólo lo que se llegó a escribir.
            const bool sameIssue = issue.id == issueId;
            if (revision.record.isEmpty() || (!sameIssue && !revision.hasDocument())) continue;
            const QDateTime when = revision.documentAt.isValid() ? revision.documentAt : revision.startedAt;
            if (newest.isValid() && when.isValid() && when < newest) continue;
            newest = when;
            previous = revision.record;
        }
    return previous;
}

QualityRecord QualityRecordService::draftFor(const QString& issueId, const QString& planRunId, int revisionNo) const {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) return {};

    const QList<PlanReport> cycles = cyclesForRecord(*issue, planRunId, revisionNo);
    QualityRecord draft = quality::draftFor(*issue, m_cases.cases(), cycles, revisionBugs(*issue, revisionNo),
                                            contextFor(*issue, cycles, revisionNo));

    // Lo ya escrito en el acta de esta revisión manda sobre lo propuesto: es lo que corrigió alguien.
    const IssueRevision* revision = issue->revision(revisionNo);
    if (!revision || revision->record.isEmpty()) return draft;
    const QualityRecord& saved = revision->record;
    draft.process = saved.process;
    draft.moduleLink = saved.moduleLink;
    draft.server = saved.server;
    draft.dbAccess = saved.dbAccess;
    draft.dbSchema = saved.dbSchema;
    draft.dbUser = saved.dbUser;
    draft.appUser = saved.appUser;
    draft.tables = saved.tables;
    draft.functions = saved.functions;
    draft.developedBy = saved.developedBy;
    draft.qaResource = saved.qaResource;
    draft.department = saved.department;
    draft.system = saved.system;
    draft.characteristics = saved.characteristics;
    draft.generalNotes = saved.generalNotes;
    draft.executionImages = saved.executionImages;
    draft.logoPath = saved.logoPath;
    // La descripción y el detalle sí se rehacen al cambiar de ciclo: son lo que el acta cuenta de esa
    // ejecución. Lo escrito a mano se conserva mientras se hable del mismo ciclo.
    const bool sameCycle = planRunId.trimmed().isEmpty() || planRunId == revision->planRunId;
    if (sameCycle && !saved.description.trimmed().isEmpty()) draft.description = saved.description;
    if (sameCycle && !saved.caseDesign.trimmed().isEmpty()) draft.caseDesign = saved.caseDesign;
    if (sameCycle && !saved.execution.trimmed().isEmpty()) draft.execution = saved.execution;
    if (sameCycle && !saved.bugs.trimmed().isEmpty()) draft.bugs = saved.bugs;
    if (sameCycle && saved.from.isValid()) draft.from = saved.from;
    if (sameCycle && saved.to.isValid()) draft.to = saved.to;
    return draft;
}

QString QualityRecordService::suggestedFileName(const QString& issueId, int revision) const {
    const Issue* issue = m_issues.find(issueId);
    const QString greq = issue && issue->isImported() ? issue->requirement.data.id : QString();
    const QString name = greq.isEmpty() ? (issue ? issue->id : QStringLiteral("issue")) : greq;
    // Un requerimiento observado levanta un acta por ronda: el número va en el nombre para que no se
    // confundan en el disco ni al adjuntarlas.
    const int number = issue ? revisionNumber(*issue, revision) : 0;
    const QString round = number > 0 ? QStringLiteral("rev%1_").arg(number) : QString();
    return QStringLiteral("ControlCalidad_%1_%2%3.docx").arg(name, round).arg(QDateTime::currentMSecsSinceEpoch());
}

QualityRecordService::GenerateResult QualityRecordService::generate(const QString& issueId, const QualityRecord& record,
                                                                    const QString& path, const QString& planRunId,
                                                                    int revision) {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) return {false, {}, tr("El issue ya no existe")};
    if (!m_writer) return {false, {}, tr("No se puede generar el acta en esta versión")};
    const QualityRecordWriteResult written = m_writer->write(record, path);
    if (!written.ok) return {false, {}, written.error};
    m_issues.setRevisionRecord(issueId, record, path, planRunId, revisionNumber(*issue, revision));
    return {true, path, {}};
}

QString QualityRecordService::summaryFor(const QString& issueId, const QualityRecord& record, QaOutcome outcome,
                                         const QString& planRunId, int revision) const {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) return {};
    const QString cycle = planRunId.trimmed().isEmpty() ? recordCycleFor(issueId, revision) : planRunId;
    const QList<PlanReport> cycles = cyclesForRecord(*issue, cycle, revision);
    return quality::summaryOf(record, outcome, cycles, contextFor(*issue, cycles, revision));
}

} // namespace qaflow
