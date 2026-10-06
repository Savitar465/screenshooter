#include "RunHistoryStore.h"

#include "application/BugStore.h"
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

RunOutcome outcomeOf(Verdict v) {
    switch (v) {
        case Verdict::Superado: return RunOutcome::Passed;
        case Verdict::Fallido: return RunOutcome::Failed;
        case Verdict::Bloqueado: return RunOutcome::Blocked;
    }
    return RunOutcome::None;
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

QList<Screenshot> RunHistoryStore::evidenceOf(const RunRecord& run) const {
    const TestCase* c = m_cases.find(run.caseId);
    const QList<Screenshot> own = c ? c->shotsOfRun(run.id) : QList<Screenshot>{};
    if (run.continuesRunId.isEmpty()) return own;
    // Lo que no se asignó a ningún paso (0) no se vuelve a probar al retomar: sigue siendo del caso.
    QList<int> inherited{0};
    for (int i = 0; i < run.steps.size(); ++i) if (run.steps[i].inherited) inherited << i + 1;
    // Primero lo heredado, que es de los pasos anteriores al que se retomó.
    return evidenceOfSteps(run.continuesRunId, inherited) + own;
}

QList<Screenshot> RunHistoryStore::evidenceOfSteps(const QString& runId, const QList<int>& steps) const {
    const RunRecord* run = steps.isEmpty() ? nullptr : findRun(runId);
    if (!run) return {};
    QList<Screenshot> out;
    for (const auto& shot : evidenceOf(*run)) if (steps.contains(shot.step)) out << shot;
    return out;
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
        return c ? PlanReport::CaseInfo{c->title, c->jiraKey} : PlanReport::CaseInfo{};
    }, m_bugs ? m_bugs->issues() : QList<IssueLink>{});
}

QString RunHistoryStore::startPlan(const QString& name, const QStringList& caseIds, const QString& planId,
                                  const QString& environment) {
    if (caseIds.isEmpty()) return {};
    PlanRun p;
    p.id = nextId(m_history.plans, QStringLiteral("PR-"));
    p.planId = planId;
    p.name = name;
    p.caseIds = caseIds;
    p.environment = environment.trimmed();
    p.startedAt = QDateTime::currentDateTime();
    m_history.plans.append(p);
    persist();
    return p.id;
}

void RunHistoryStore::noteCycleRevision(const QString& planRunId, const QString& issueId, int revision) {
    if (issueId.trimmed().isEmpty()) return;
    for (auto& p : m_history.plans) {
        if (p.id != planRunId) continue;
        if (p.issueId == issueId && p.revision == revision) return;
        p.issueId = issueId;
        p.revision = revision;
        persist();
        return;
    }
}

QString RunHistoryStore::lastEnvironment() const {
    // De atrás hacia delante: los ciclos se añaden en orden, así que el último que lo indique es el
    // más reciente.
    for (auto it = m_history.plans.crbegin(); it != m_history.plans.crend(); ++it)
        if (!it->environment.trimmed().isEmpty()) return it->environment.trimmed();
    return {};
}

void RunHistoryStore::finishPlan(const QString& planRunId) {
    for (auto& p : m_history.plans) {
        if (p.id != planRunId || p.isFinished()) continue;
        p.finishedAt = QDateTime::currentDateTime();
        persist();
        return;
    }
}

void RunHistoryStore::setPlanCases(const QString& planRunId, const QStringList& caseIds) {
    for (auto& p : m_history.plans) {
        if (p.id != planRunId) continue;
        if (p.caseIds == caseIds) return;
        p.caseIds = caseIds;
        persist();
        return;
    }
}

bool RunHistoryStore::reopenPlan(const QString& planRunId, const QString& environment) {
    for (auto& p : m_history.plans) {
        if (p.id != planRunId) continue;
        if (!p.isFinished()) return false;
        p.finishedAt = QDateTime();
        if (p.environment.trimmed().isEmpty()) p.environment = environment.trimmed();
        ++p.continuations;
        persist();
        return true;
    }
    return false;
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

void RunHistoryStore::assignTestKeys(const QHash<QString, QString>& testKeyByRunId) {
    bool changed = false;
    for (auto& r : m_history.runs) {
        const QString key = testKeyByRunId.value(r.id).trimmed();
        if (key.isEmpty() || r.testKey == key) continue;
        r.testKey = key;
        changed = true;
    }
    if (changed) persist();
}

QString RunHistoryStore::reserveRunId(const QStringList& held) const {
    const QString prefix = QStringLiteral("R-");
    QString id = nextId(m_history.runs, prefix);
    int number = id.mid(prefix.size()).toInt();
    for (const QString& other : held) {
        bool ok = false;
        const int n = other.mid(prefix.size()).toInt(&ok);
        if (ok && other.startsWith(prefix) && n >= number) number = n + 1;
    }
    return prefix + QStringLiteral("%1").arg(number, 4, 10, QLatin1Char('0'));
}

QList<IssueLink> RunHistoryStore::bugsOfRun(const RunRecord& run) const {
    if (!m_bugs) return {};
    // La cadena de ejecuciones que se retoman es finita, pero se acota por si un history.json editado a
    // mano la cerrara en círculo.
    QList<const RunRecord*> chain{&run};
    for (const RunRecord* r = &run; !r->continuesRunId.isEmpty() && chain.size() < 100;) {
        r = findRun(r->continuesRunId);
        if (!r || chain.contains(r)) break;
        chain << r;
    }
    QList<IssueLink> out;
    for (const auto& bug : m_bugs->issues())
        if (std::any_of(chain.cbegin(), chain.cend(), [&bug](const RunRecord* r) { return PlanReport::foundIn(*r, bug); })) out << bug;
    std::sort(out.begin(), out.end(), [](const IssueLink& a, const IssueLink& b) { return a.createdAt > b.createdAt; });
    return out;
}

QList<IssueLink> RunHistoryStore::bugsOfRun(const QString& runId) const {
    const RunRecord* run = runId.isEmpty() ? nullptr : findRun(runId);
    return run ? bugsOfRun(*run) : QList<IssueLink>{};
}

RunRecord RunHistoryStore::addRun(RunRecord record) {
    // El id reservado al arrancar la ejecución es con el que la conocen los bugs que se reportaron
    // mientras corría; sólo se descarta si entretanto lo cogió otra.
    const bool taken = record.id.trimmed().isEmpty() ||
                       std::any_of(m_history.runs.cbegin(), m_history.runs.cend(),
                                   [&record](const RunRecord& r) { return r.id == record.id; });
    if (taken) record.id = nextId(m_history.runs, QStringLiteral("R-"));
    m_history.runs.append(record);
    persist();
    return record;
}

bool RunHistoryStore::removePlanRun(const QString& planRunId) {
    auto plan = std::find_if(m_history.plans.begin(), m_history.plans.end(), [&](const PlanRun& p) { return p.id == planRunId; });
    if (plan == m_history.plans.end()) return false;

    // Los casos cuya «última ejecución» es una de las que se van: sólo a ésos hay que recalcularla.
    QStringList runIds, staleCases;
    for (const auto& r : m_history.runs) {
        if (r.planRunId != planRunId) continue;
        runIds << r.id;
        const QList<RunRecord> ofCase = runsForCase(r.caseId);   // la más reciente primero
        if (!ofCase.isEmpty() && ofCase.first().planRunId == planRunId && !staleCases.contains(r.caseId)) staleCases << r.caseId;
    }
    m_history.plans.erase(plan);
    m_history.runs.erase(std::remove_if(m_history.runs.begin(), m_history.runs.end(), [&](const RunRecord& r) { return r.planRunId == planRunId; }),
                         m_history.runs.end());

    m_cases.releaseShotsOfRuns(runIds);
    for (const auto& caseId : staleCases) {
        const QList<RunRecord> left = runsForCase(caseId);
        const LastRun last = left.isEmpty() ? LastRun{} : LastRun{outcomeOf(left.first().verdict), left.first().finishedAt};
        m_cases.updateCase(caseId, [last](TestCase& c) { c.lastRun = last; });
    }
    persist();
    return true;
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
