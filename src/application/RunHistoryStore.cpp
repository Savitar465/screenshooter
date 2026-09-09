#include "RunHistoryStore.h"

#include "application/TestCaseStore.h"

#include <algorithm>

namespace qaflow {

namespace {
/// Siguiente id "PREFIX-0001" a partir del mayor número ya usado.
template <typename T>
QString nextId(const QList<T>& items, const QString& prefix) {
    int maxNum = 0;
    for (const auto& it : items) {
        bool ok = false;
        const int n = it.id.mid(prefix.size()).toInt(&ok);
        if (ok) maxNum = std::max(maxNum, n);
    }
    return prefix + QStringLiteral("%1").arg(maxNum + 1, 4, 10, QLatin1Char('0'));
}
} // namespace

RunHistoryStore::RunHistoryStore(std::shared_ptr<IRunHistoryRepository> repo, TestCaseStore& cases, QObject* parent)
    : QObject(parent), m_repo(std::move(repo)), m_cases(cases) {}

void RunHistoryStore::load() {
    auto loaded = m_repo ? m_repo->loadHistory() : std::nullopt;
    m_history = loaded ? *loaded : RunHistory{};
    emit historyChanged();
}

const RunRecord* RunHistoryStore::findRun(const QString& id) const {
    auto it = std::find_if(m_history.runs.cbegin(), m_history.runs.cend(), [&](const RunRecord& r) { return r.id == id; });
    return it == m_history.runs.cend() ? nullptr : &*it;
}

const PlanRun* RunHistoryStore::findPlan(const QString& id) const {
    auto it = std::find_if(m_history.plans.cbegin(), m_history.plans.cend(), [&](const PlanRun& p) { return p.id == id; });
    return it == m_history.plans.cend() ? nullptr : &*it;
}

QList<RunRecord> RunHistoryStore::runsForCase(const QString& caseId) const {
    QList<RunRecord> out;
    for (const auto& r : m_history.runs) if (r.caseId == caseId) out.prepend(r);
    return out;
}

QList<RunRecord> RunHistoryStore::runsForPlan(const QString& planRunId) const {
    QList<RunRecord> out;
    for (const auto& r : m_history.runs) if (r.planRunId == planRunId) out.append(r);
    return out;
}

PlanReport RunHistoryStore::report(const QString& planRunId) const {
    const PlanRun* plan = findPlan(planRunId);
    if (!plan) return PlanReport{};
    return PlanReport::build(*plan, runsForPlan(planRunId), [this](const QString& caseId) {
        const TestCase* c = m_cases.find(caseId);
        return c ? PlanReport::CaseInfo{c->title, c->jiraKey, c->testKey} : PlanReport::CaseInfo{};
    });
}

QString RunHistoryStore::startPlan(const QString& name, const QStringList& caseIds, const QString& planId) {
    if (caseIds.isEmpty()) return {};
    PlanRun p;
    p.id = nextId(m_history.plans, QStringLiteral("PR-"));
    p.planId = planId;
    p.name = name;
    p.caseIds = caseIds;
    p.startedAt = QDateTime::currentDateTime();
    m_history.plans.append(p);
    persist();
    return p.id;
}

void RunHistoryStore::finishPlan(const QString& planRunId) {
    for (auto& p : m_history.plans) {
        if (p.id != planRunId || p.isFinished()) continue;
        p.finishedAt = QDateTime::currentDateTime();
        persist();
        return;
    }
}

int RunHistoryStore::adoptLooseEvidence(const QString& runningCaseId) {
    // Los ids primero: adoptar reescribe la lista de casos y dejaría la iteración colgando.
    QStringList pending;
    for (const auto& c : m_cases.cases()) {
        if (c.id == runningCaseId) continue;
        for (const auto& s : c.shots)
            if (s.runId.isEmpty()) { pending << c.id; break; }
    }
    int moved = 0;
    for (const auto& caseId : pending) {
        const QList<RunRecord> runs = runsForCase(caseId);   // la más reciente primero
        moved += m_cases.adoptLooseShots(caseId, runs.isEmpty() ? QString() : runs.first().id);
    }
    return moved;
}

void RunHistoryStore::markPublished(const QString& planRunId, const QString& zephyrCycleId) {
    if (zephyrCycleId.trimmed().isEmpty()) return;
    for (auto& p : m_history.plans) {
        if (p.id != planRunId) continue;
        // Republicar crea otro ciclo en Zephyr: el que vale es el último, que es donde están estos
        // resultados ahora.
        p.zephyrCycleId = zephyrCycleId.trimmed();
        p.publishedAt = QDateTime::currentDateTime();
        persist();
        return;
    }
}

RunRecord RunHistoryStore::addRun(RunRecord record) {
    record.id = nextId(m_history.runs, QStringLiteral("R-"));
    m_history.runs.append(record);
    persist();
    return record;
}

bool RunHistoryStore::save() {
    if (!m_repo) return false;
    if (m_repo->saveHistory(m_history)) return true;
    emit saveFailed(tr("el historial de ejecuciones"));
    return false;
}

void RunHistoryStore::persist() {
    save();
    emit historyChanged();
}

} // namespace qaflow
