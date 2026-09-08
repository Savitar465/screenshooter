#include "BugStore.h"

#include <algorithm>

namespace qaflow {

BugStore::BugStore(std::shared_ptr<IBugRepository> repo, QObject* parent) : QObject(parent), m_repo(std::move(repo)) {}

void BugStore::load() {
    auto loaded = m_repo ? m_repo->loadLedger() : std::nullopt;
    m_ledger = loaded ? *loaded : BugLedger{};
    emit bugsChanged();
}

// ---- Issues --------------------------------------------------------------------------------

QList<IssueLink> BugStore::issuesForCase(const QString& caseId) const {
    QList<IssueLink> out;
    for (const auto& i : m_ledger.issues) if (i.caseId == caseId) out.prepend(i);
    return out;
}

const IssueLink* BugStore::findIssue(const QString& key) const {
    auto it = std::find_if(m_ledger.issues.cbegin(), m_ledger.issues.cend(), [&](const IssueLink& i) { return i.key == key; });
    return it == m_ledger.issues.cend() ? nullptr : &*it;
}

int BugStore::openIssueCount() const {
    return static_cast<int>(std::count_if(m_ledger.issues.cbegin(), m_ledger.issues.cend(), [](const IssueLink& i) { return !i.resolved; }));
}

void BugStore::recordIssue(const IssueLink& link) {
    for (auto& i : m_ledger.issues) {
        if (i.key != link.key || i.tracker != link.tracker) continue;
        i = link;
        persist();
        return;
    }
    m_ledger.issues.append(link);
    persist();
}

void BugStore::updateStatus(const QString& key, const QString& status, bool resolved) {
    for (auto& i : m_ledger.issues) {
        if (i.key != key) continue;
        i.status = status;
        i.resolved = resolved;
        i.statusCheckedAt = QDateTime::currentDateTime();
        persist();
        return;
    }
}

void BugStore::forgetIssue(const QString& key) {
    const int before = m_ledger.issues.size();
    m_ledger.issues.erase(std::remove_if(m_ledger.issues.begin(), m_ledger.issues.end(), [&](const IssueLink& i) { return i.key == key; }), m_ledger.issues.end());
    if (m_ledger.issues.size() != before) persist();
}

// ---- Pendientes ----------------------------------------------------------------------------

const PendingBug* BugStore::findPending(const QString& id) const {
    auto it = std::find_if(m_ledger.pending.cbegin(), m_ledger.pending.cend(), [&](const PendingBug& p) { return p.id == id; });
    return it == m_ledger.pending.cend() ? nullptr : &*it;
}

QString BugStore::enqueue(const BugReport& report, const QString& error) {
    int maxNum = 0;
    for (const auto& p : m_ledger.pending) {
        bool ok = false;
        const int n = p.id.mid(2).toInt(&ok);
        if (ok) maxNum = std::max(maxNum, n);
    }
    PendingBug p;
    p.id = QStringLiteral("Q-%1").arg(maxNum + 1, 4, 10, QLatin1Char('0'));
    p.report = report;
    p.createdAt = QDateTime::currentDateTime();
    p.lastError = error;
    p.attempts = 1;
    m_ledger.pending.append(p);
    persist();
    return p.id;
}

void BugStore::markAttempt(const QString& id, const QString& error) {
    for (auto& p : m_ledger.pending) {
        if (p.id != id) continue;
        ++p.attempts;
        p.lastError = error;
        persist();
        return;
    }
}

void BugStore::removePending(const QString& id) {
    const int before = m_ledger.pending.size();
    m_ledger.pending.erase(std::remove_if(m_ledger.pending.begin(), m_ledger.pending.end(), [&](const PendingBug& p) { return p.id == id; }), m_ledger.pending.end());
    if (m_ledger.pending.size() != before) persist();
}

bool BugStore::save() {
    if (!m_repo) return false;
    if (m_repo->saveLedger(m_ledger)) return true;
    emit saveFailed(tr("los bugs reportados"));
    return false;
}

void BugStore::persist() {
    save();
    emit bugsChanged();
}

} // namespace qaflow
