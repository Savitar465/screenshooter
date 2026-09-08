#include "BugReportService.h"

#include "application/BugStore.h"
#include "application/RunController.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"

namespace qaflow {

BugReportService::BugReportService(std::shared_ptr<IIssueTracker> tracker, TestCaseStore& cases, RunController& run,
                                   SettingsStore& settings, BugStore& bugs, QObject* parent)
    : QObject(parent), m_tracker(std::move(tracker)), m_cases(cases), m_run(run), m_settings(settings), m_bugs(bugs) {
    // Al cambiar de gestor o de proyecto, los metadatos cacheados dejan de valer.
    connect(&m_settings, &SettingsStore::trackerChanged, this, [this]() {
        const TrackerSettings& t = m_settings.tracker();
        const QString id = toString(t.kind) + QLatin1Char('|') + t.project + QLatin1Char('@') + t.baseUrl();
        if (id != m_metadataFor && !m_metadata.isEmpty()) {
            m_metadata = ProjectMetadata{};
            m_metadataFor.clear();
            emit metadataChanged();
        }
    });
}

BugReport BugReportService::draftFromCurrentContext() const {
    BugReport b;
    const TestCase* c = m_cases.selected();
    if (!c) return b;

    b.linkedCaseId = c->id;
    b.linkedStoryKey = c->jiraKey;
    b.components = c->component.isEmpty() ? QStringList{} : QStringList{c->component};
    QStringList lines;
    for (int i = 0; i < c->steps.size(); ++i) lines << QStringLiteral("%1. %2").arg(i + 1).arg(c->steps[i].action);
    b.stepsToReproduce = lines.join(QLatin1Char('\n'));

    const RunState& r = m_run.state();
    const int failIdx = r.caseId == c->id ? r.firstFailIndex() : -1;
    if (failIdx >= 0 && failIdx < c->steps.size()) {
        const TestStep& failed = c->steps[failIdx];
        b.title = tr("[%1] Falla en paso %2: %3").arg(c->suite).arg(failIdx + 1).arg(failed.action);
        b.expected = failed.expected;
        b.actual = r.results[failIdx].note;
    }
    for (const auto& s : c->shots) b.attachmentPaths << s.path;
    b.priority = BugReport::jiraPriorityFor(b.severity);
    return b;
}

IssueLink BugReportService::linkFor(const BugReport& bug, const IssueResult& r) const {
    IssueLink l;
    l.key = r.key;
    l.url = r.url;
    l.title = bug.title.trimmed();
    l.caseId = bug.linkedCaseId;
    l.tracker = toString(m_settings.tracker().kind);
    l.severity = bug.severity;
    l.createdAt = QDateTime::currentDateTime();
    return l;
}

void BugReportService::submit(const BugReport& bug, std::function<void(const SubmitResult&)> done) {
    if (!m_tracker) { done(SubmitResult{false, false, {}, {}, tr("Integración no disponible")}); return; }
    m_tracker->createIssue(m_settings.tracker(), bug, [this, bug, done](const IssueResult& r) {
        SubmitResult out;
        out.error = r.error;
        out.attachmentsUploaded = r.attachmentsUploaded;
        if (r.ok) {
            out.ok = true;
            out.key = r.key;
            out.url = r.url;
            m_bugs.recordIssue(linkFor(bug, r));
        } else if (r.retryable) {
            out.queued = true;
            m_bugs.enqueue(bug, r.error);
        }
        done(out);
    });
}

void BugReportService::retryPending(std::function<void(const RetryResult&)> done) {
    QList<QString> ids;
    for (const auto& p : m_bugs.pending()) ids << p.id;
    retryNext(ids, RetryResult{}, std::move(done));
}

void BugReportService::retryNext(QList<QString> ids, RetryResult acc, std::function<void(const RetryResult&)> done) {
    if (ids.isEmpty() || !m_tracker) { done(acc); return; }
    const QString id = ids.takeFirst();
    const PendingBug* p = m_bugs.findPending(id);
    if (!p) { retryNext(ids, acc, std::move(done)); return; }
    const BugReport bug = p->report;
    m_tracker->createIssue(m_settings.tracker(), bug, [this, id, bug, ids, acc, done](const IssueResult& r) mutable {
        if (r.ok) {
            m_bugs.recordIssue(linkFor(bug, r));
            m_bugs.removePending(id);
            ++acc.sent;
            acc.keys << r.key;
            retryNext(ids, acc, std::move(done));
            return;
        }
        m_bugs.markAttempt(id, r.error);
        ++acc.failed;
        // Sin red no tiene sentido seguir; un rechazo del contenido sí deja pasar al siguiente.
        if (r.retryable) { acc.failed += ids.size(); done(acc); return; }
        retryNext(ids, acc, std::move(done));
    });
}

void BugReportService::discardPending(const QString& id) { m_bugs.removePending(id); }

void BugReportService::refreshStatuses(bool onlyOpen, std::function<void(const RefreshResult&)> done) {
    QStringList keys;
    const QString tracker = toString(m_settings.tracker().kind);
    for (const auto& i : m_bugs.issues()) if (i.tracker == tracker && (!onlyOpen || !i.resolved)) keys << i.key;
    refreshNext(keys, RefreshResult{}, std::move(done));
}

void BugReportService::refreshNext(QStringList keys, RefreshResult acc, std::function<void(const RefreshResult&)> done) {
    if (keys.isEmpty() || !m_tracker) { done(acc); return; }
    const QString key = keys.takeFirst();
    m_tracker->fetchStatus(m_settings.tracker(), key, [this, key, keys, acc, done](const IssueStatus& s) mutable {
        if (s.ok) { m_bugs.updateStatus(key, s.status, s.resolved); ++acc.updated; }
        else ++acc.failed;
        refreshNext(keys, acc, std::move(done));
    });
}

void BugReportService::testConnection(std::function<void(const ConnectionResult&)> done) {
    if (!m_tracker) { done(ConnectionResult{false, {}, tr("Integración no disponible")}); return; }
    m_tracker->testConnection(m_settings.tracker(), std::move(done));
}

void BugReportService::loadMetadata(bool force, std::function<void(const MetadataResult&)> done) {
    const TrackerSettings& t = m_settings.tracker();
    const QString id = toString(t.kind) + QLatin1Char('|') + t.project + QLatin1Char('@') + t.baseUrl();
    if (!force && id == m_metadataFor && !m_metadata.isEmpty()) { done(MetadataResult{true, m_metadata, {}}); return; }
    if (!m_tracker) { done(MetadataResult{false, {}, tr("Integración no disponible")}); return; }
    m_tracker->fetchMetadata(t, [this, id, done](const MetadataResult& r) {
        if (r.ok) {
            m_metadata = r.metadata;
            m_metadataFor = id;
            emit metadataChanged();
        }
        done(r);
    });
}

bool BugReportService::searchesAssigneesOnServer() const {
    const TrackerSettings& t = m_settings.tracker();
    return m_tracker && t.connected && m_tracker->canSearchAssignees(t);
}

void BugReportService::searchAssignees(const QString& query, std::function<void(const AssigneeSearch&)> done) {
    const TrackerSettings& t = m_settings.tracker();
    // Filtrado local: sin gestor, sin conexión o con uno que no sabe buscar (GitHub, GitLab, Azure).
    auto fromMetadata = [this, query]() {
        const QString needle = query.trimmed();
        QList<Assignee> out;
        for (const auto& a : m_metadata.assignees)
            if (needle.isEmpty() || a.name.contains(needle, Qt::CaseInsensitive) || a.id.contains(needle, Qt::CaseInsensitive))
                out.append(a);
        return out;
    };
    if (!searchesAssigneesOnServer()) {
        done(AssigneeSearch{true, fromMetadata(), {}});
        return;
    }
    m_tracker->searchAssignees(t, query, [done, fromMetadata](const AssigneeSearch& r) {
        // Si la búsqueda falla (red, permisos), el formulario se queda con lo que ya tenía y lo dice.
        if (!r.ok) { done(AssigneeSearch{false, fromMetadata(), r.error}); return; }
        done(r);
    });
}

} // namespace qaflow
