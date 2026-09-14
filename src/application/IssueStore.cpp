#include "IssueStore.h"

#include "application/RunHistoryStore.h"

#include <QSet>

#include <algorithm>

namespace qaflow {

namespace {
/// La conexión tal y como se guarda: sin espacios ni barra final.
QString cleanConnection(const QString& connection) {
    QString c = connection.trimmed();
    while (c.endsWith(QLatin1Char('/'))) c.chop(1);
    return c;
}

/// La misma dirección con o sin barra final, o con otras mayúsculas, es la misma conexión.
bool sameConnection(const QString& a, const QString& b) {
    return cleanConnection(a).compare(cleanConnection(b), Qt::CaseInsensitive) == 0;
}
} // namespace

IssueStore::IssueStore(std::shared_ptr<IIssueRepository> repo, QObject* parent) : QObject(parent), m_repo(std::move(repo)) {}

void IssueStore::load() {
    const std::optional<QList<Issue>> loaded = m_repo ? m_repo->loadIssues() : std::optional<QList<Issue>>(QList<Issue>{});
    m_readOnly = !loaded.has_value();
    m_issues = loaded.value_or(QList<Issue>{});
    if (!find(m_selectedId)) m_selectedId = m_issues.isEmpty() ? QString() : m_issues.first().id;
    emit issuesChanged();
    emit selectionChanged(m_selectedId);
    if (m_readOnly) emit loadFailed(tr("No se pudieron leer los issues del proyecto: no se guardarán cambios en ellos para no perderlos"));
}

bool IssueStore::save() {
    if (!m_readOnly && (!m_repo || m_repo->saveIssues(m_issues))) return true;
    emit saveFailed(tr("los issues"));
    return false;
}

void IssueStore::persist(const QString& changedId) {
    save();
    emit issuesChanged();
    if (!changedId.isEmpty()) emit issueChanged(changedId);
}

const Issue* IssueStore::find(const QString& id) const {
    const auto it = std::find_if(m_issues.cbegin(), m_issues.cend(), [&id](const Issue& i) { return i.id == id; });
    return it == m_issues.cend() ? nullptr : &*it;
}

Issue* IssueStore::findMutable(const QString& id) {
    const auto it = std::find_if(m_issues.begin(), m_issues.end(), [&id](const Issue& i) { return i.id == id; });
    return it == m_issues.end() ? nullptr : &*it;
}

QList<Issue> IssueStore::issuesForCase(const QString& caseId) const {
    QList<Issue> out;
    for (const auto& i : m_issues)
        if (i.caseIds.contains(caseId)) out << i;
    return out;
}

const Issue* IssueStore::findByRequirement(const QString& connection, const QString& requirementId) const {
    const auto it = std::find_if(m_issues.cbegin(), m_issues.cend(), [&](const Issue& i) {
        return i.isImported() && i.requirement.data.id == requirementId && sameConnection(i.requirement.connection, connection);
    });
    return it == m_issues.cend() ? nullptr : &*it;
}

int IssueStore::changedCount() const {
    return int(std::count_if(m_issues.cbegin(), m_issues.cend(), [](const Issue& i) { return !i.requirement.changes.isEmpty(); }));
}

void IssueStore::select(const QString& id) {
    if (id == m_selectedId || (!id.isEmpty() && !find(id))) return;
    m_selectedId = id;
    emit selectionChanged(id);
}

QString IssueStore::nextId() const {
    int maxNum = 0;
    for (const auto& i : m_issues) {
        bool ok = false;
        const int n = i.id.mid(3).toInt(&ok);   // IS-0001
        if (ok) maxNum = std::max(maxNum, n);
    }
    return QStringLiteral("IS-%1").arg(maxNum + 1, 4, 10, QLatin1Char('0'));
}

QString IssueStore::createIssue(const QString& title) {
    Issue issue;
    issue.id = nextId();
    issue.title = title.trimmed().isEmpty() ? tr("Issue sin título") : title.trimmed();
    issue.createdAt = issue.updatedAt = QDateTime::currentDateTime();
    m_issues.append(issue);
    persist(issue.id);
    m_selectedId = issue.id;
    emit selectionChanged(issue.id);
    return issue.id;
}

void IssueStore::updateIssue(const QString& id, const std::function<void(Issue&)>& mutate) {
    Issue* issue = findMutable(id);
    if (!issue) return;
    mutate(*issue);
    issue->id = id;   // la identidad no cambia desde fuera
    issue->updatedAt = QDateTime::currentDateTime();
    persist(id);
}

void IssueStore::removeIssue(const QString& id) {
    const auto it = std::find_if(m_issues.begin(), m_issues.end(), [&id](const Issue& i) { return i.id == id; });
    if (it == m_issues.end()) return;
    const qsizetype index = it - m_issues.begin();
    m_issues.erase(it);
    persist();
    if (m_selectedId != id) return;
    m_selectedId = m_issues.isEmpty() ? QString() : m_issues[std::min(index, m_issues.size() - 1)].id;
    emit selectionChanged(m_selectedId);
}

void IssueStore::linkCase(const QString& issueId, const QString& caseId) {
    const Issue* issue = find(issueId);
    if (!issue || caseId.isEmpty() || issue->caseIds.contains(caseId)) return;
    updateIssue(issueId, [&caseId](Issue& i) { i.caseIds << caseId; });
}

void IssueStore::unlinkCase(const QString& issueId, const QString& caseId) {
    const Issue* issue = find(issueId);
    if (!issue || !issue->caseIds.contains(caseId)) return;
    updateIssue(issueId, [&caseId](Issue& i) { i.caseIds.removeAll(caseId); });
}

void IssueStore::linkPlan(const QString& issueId, const QString& planId) {
    const Issue* issue = find(issueId);
    if (!issue || planId.isEmpty() || issue->planIds.contains(planId)) return;
    updateIssue(issueId, [&planId](Issue& i) { i.planIds << planId; });
}

void IssueStore::unlinkPlan(const QString& issueId, const QString& planId) {
    const Issue* issue = find(issueId);
    if (!issue || !issue->planIds.contains(planId)) return;
    updateIssue(issueId, [&planId](Issue& i) { i.planIds.removeAll(planId); });
}

// ---- Importación -------------------------------------------------------------------------------

QList<IssueStore::ImportCandidate> IssueStore::previewImport(const QList<ExternalRequirement>& requirements, const QString& connection) const {
    QList<ImportCandidate> out;
    QSet<QString> seen;
    for (const auto& r : requirements) {
        if (r.id.isEmpty() || seen.contains(r.id)) continue;
        seen.insert(r.id);
        ImportCandidate candidate;
        candidate.requirement = r;
        if (const Issue* existing = findByRequirement(connection, r.id)) {
            candidate.issueId = existing->id;
            candidate.changes = diffRequirement(existing->requirement.data, r);
            candidate.kind = candidate.changes.isEmpty() ? ImportCandidate::Kind::Unchanged : ImportCandidate::Kind::Changed;
        }
        out << candidate;
    }
    return out;
}

IssueStore::ImportResult IssueStore::importRequirements(const QList<ExternalRequirement>& requirements, const QString& connection,
                                                        const QDateTime& fetchedAt) {
    ImportResult result;
    const QString conn = cleanConnection(connection);
    const QDateTime now = QDateTime::currentDateTime();
    QSet<QString> seen;
    bool touched = false;
    for (const auto& r : requirements) {
        if (r.id.isEmpty() || seen.contains(r.id)) continue;
        seen.insert(r.id);
        touched = true;
        if (const Issue* found = findByRequirement(conn, r.id)) {
            Issue* issue = findMutable(found->id);
            const QList<RequirementChange> changes = diffRequirement(issue->requirement.data, r);
            // Sólo lo extraído: título, notas, prioridad, estado y asociaciones son de QAflow.
            issue->requirement.data = r;
            issue->requirement.fetchedAt = fetchedAt;
            issue->requirement.missing = false;
            if (changes.isEmpty()) continue;
            issue->requirement.changes = mergeChanges(issue->requirement.changes, changes);
            issue->updatedAt = now;
            result.updated << issue->id;
            continue;
        }
        Issue issue;
        issue.id = nextId();
        issue.title = r.summary.trimmed().isEmpty() ? tr("Requerimiento %1").arg(r.id) : r.summary.simplified();
        issue.priority = priorityFromRequirement(r.priority);
        issue.requirement.connection = conn;
        issue.requirement.data = r;
        issue.requirement.importedAt = fetchedAt;
        issue.requirement.fetchedAt = fetchedAt;
        issue.createdAt = issue.updatedAt = now;
        m_issues.append(issue);
        result.created << issue.id;
    }
    if (!touched) return result;
    persist();
    for (const auto& id : result.created + result.updated) emit issueChanged(id);
    if (!result.created.isEmpty()) {
        m_selectedId = result.created.first();
        emit selectionChanged(m_selectedId);
    }
    return result;
}

QString IssueStore::openForRequirement(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt) {
    if (requirement.id.isEmpty()) return {};
    importRequirements({requirement}, connection, fetchedAt);
    const Issue* issue = findByRequirement(connection, requirement.id);
    if (!issue) return {};
    select(issue->id);   // el recién creado ya queda seleccionado; el que ya existía se trae al frente
    return issue->id;
}

int IssueStore::markInboxRead(const QList<ExternalRequirement>& inbox, const QString& connection, const QDateTime& fetchedAt) {
    QSet<QString> present;
    for (const auto& r : inbox) present.insert(r.id);
    int missing = 0;
    bool touched = false;
    QStringList changed;
    for (auto& issue : m_issues) {
        if (!issue.isImported() || !sameConnection(issue.requirement.connection, connection)) continue;
        touched = true;
        const bool gone = !present.contains(issue.requirement.data.id);
        if (gone) ++missing;
        else issue.requirement.fetchedAt = fetchedAt;
        if (issue.requirement.missing == gone) continue;
        issue.requirement.missing = gone;
        changed << issue.id;
    }
    if (!touched) return 0;
    persist();
    for (const auto& id : changed) emit issueChanged(id);
    return missing;
}

void IssueStore::setRequirementDetail(const QString& issueId, const RequirementDetail& detail, const QDateTime& fetchedAt) {
    const Issue* issue = find(issueId);
    if (!issue || !issue->isImported()) return;
    updateIssue(issueId, [&](Issue& i) {
        i.requirement.detail = detail;
        i.requirement.detailFetchedAt = fetchedAt;
    });
}

void IssueStore::acknowledgeChanges(const QString& issueId) {
    const Issue* issue = find(issueId);
    if (!issue || issue->requirement.changes.isEmpty()) return;
    updateIssue(issueId, [](Issue& i) { i.requirement.changes.clear(); });
}

QList<RunRecord> IssueStore::runsOf(const Issue& issue, const RunHistoryStore& history) {
    QList<RunRecord> runs;
    QSet<QString> cases;
    for (const auto& caseId : issue.caseIds) {
        if (cases.contains(caseId)) continue;
        cases.insert(caseId);
        runs += history.runsForCase(caseId);
    }
    std::sort(runs.begin(), runs.end(), [](const RunRecord& a, const RunRecord& b) {
        const QDateTime ta = a.finishedAt.isValid() ? a.finishedAt : a.startedAt;
        const QDateTime tb = b.finishedAt.isValid() ? b.finishedAt : b.startedAt;
        // Dos ejecuciones en el mismo segundo: la de id posterior es la más reciente.
        return ta != tb ? ta > tb : a.id > b.id;
    });
    return runs;
}

} // namespace qaflow
