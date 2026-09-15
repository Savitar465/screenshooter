#include "QualityRecordService.h"

#include "application/BugStore.h"
#include "application/IssueStore.h"
#include "application/RunHistoryStore.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"

#include <algorithm>

namespace qaflow {

QualityRecordService::QualityRecordService(IssueStore& issues, TestCaseStore& cases, RunHistoryStore& history, BugStore& bugs,
                                           SettingsStore& settings, std::shared_ptr<IQualityRecordWriter> writer, QObject* parent)
    : QObject(parent), m_issues(issues), m_cases(cases), m_history(history), m_bugs(bugs), m_settings(settings),
      m_writer(std::move(writer)) {}

QDateTime QualityRecordService::revisionStart(const Issue& issue) const {
    if (const IssueRevision* open = issue.currentRevision()) return open->startedAt;
    if (!issue.revisions.isEmpty()) return issue.revisions.last().startedAt;
    return {};
}

QList<RunRecord> QualityRecordService::revisionRuns(const Issue& issue) const {
    const QDateTime since = revisionStart(issue);
    QList<RunRecord> runs = IssueStore::runsOf(issue, m_history);
    if (!since.isValid()) return runs;
    const auto end = std::remove_if(runs.begin(), runs.end(), [&since](const RunRecord& run) {
        const QDateTime when = run.finishedAt.isValid() ? run.finishedAt : run.startedAt;
        return when.isValid() && when < since;
    });
    runs.erase(end, runs.end());
    return runs;
}

QList<IssueLink> QualityRecordService::revisionBugs(const Issue& issue) const {
    const QDateTime since = revisionStart(issue);
    QList<IssueLink> out;
    for (const auto& bug : m_bugs.issues()) {
        if (!issue.caseIds.contains(bug.caseId)) continue;
        if (since.isValid() && bug.createdAt.isValid() && bug.createdAt < since) continue;
        out << bug;
    }
    return out;
}

IssueProgress QualityRecordService::progressFor(const QString& issueId) const {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) return {};
    return issueProgress(*issue, m_cases.cases(), IssueStore::runsOf(*issue, m_history), m_bugs.issues(), revisionStart(*issue));
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

QualityRecord QualityRecordService::draftFor(const QString& issueId) const {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) return {};

    quality::DraftContext context;
    context.qaResource = m_settings.requirementSource().user;
    context.revisionNumber = issue->revisions.isEmpty() ? 1 : issue->revisions.last().number;
    context.previous = previousRecord(issueId);

    const QDateTime since = revisionStart(*issue);
    for (const auto& bug : m_bugs.issues())
        if (issue->caseIds.contains(bug.caseId) && since.isValid() && bug.createdAt.isValid() && bug.createdAt < since)
            context.previousBugs << bug;

    QualityRecord draft = quality::draftFor(*issue, m_cases.cases(), revisionRuns(*issue), revisionBugs(*issue), context);

    // Lo ya escrito en el acta de esta revisión manda sobre lo propuesto: es lo que corrigió alguien.
    const IssueRevision* revision = issue->revisions.isEmpty() ? nullptr : &issue->revisions.last();
    if (!revision || revision->record.isEmpty()) return draft;
    const QualityRecord& saved = revision->record;
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
    if (saved.from.isValid()) draft.from = saved.from;
    if (saved.to.isValid()) draft.to = saved.to;
    return draft;
}

QString QualityRecordService::suggestedFileName(const QString& issueId) const {
    const Issue* issue = m_issues.find(issueId);
    const QString greq = issue && issue->isImported() ? issue->requirement.data.id : QString();
    const QString name = greq.isEmpty() ? (issue ? issue->id : QStringLiteral("issue")) : greq;
    return QStringLiteral("ControlCalidad_%1_%2.docx").arg(name).arg(QDateTime::currentMSecsSinceEpoch());
}

QualityRecordService::GenerateResult QualityRecordService::generate(const QString& issueId, const QualityRecord& record,
                                                                    const QString& path) {
    if (!m_issues.find(issueId)) return {false, {}, tr("El issue ya no existe")};
    if (!m_writer) return {false, {}, tr("No se puede generar el acta en esta versión")};
    const QualityRecordWriteResult written = m_writer->write(record, path);
    if (!written.ok) return {false, {}, written.error};
    m_issues.setRevisionRecord(issueId, record, path);
    return {true, path, {}};
}

QString QualityRecordService::summaryFor(const QString& issueId, const QualityRecord& record, QaOutcome outcome) const {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) return {};
    return quality::summaryOf(record, outcome, revisionRuns(*issue));
}

} // namespace qaflow
