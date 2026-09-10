#include "PlanStore.h"

#include "application/RunHistoryStore.h"
#include "application/SeedData.h"
#include "application/TestCaseStore.h"

#include <algorithm>

namespace qaflow {

namespace {
constexpr int kDefaultSecsPerStep = 3 * 60;

/// Media de segundos por paso de todas las ejecuciones con tiempo medido.
double averageSecsPerStep(const QList<RunRecord>& runs, int* usedRuns = nullptr) {
    qint64 secs = 0;
    int steps = 0, n = 0;
    for (const auto& r : runs) {
        if (r.durationSecs <= 0 || r.steps.isEmpty()) continue;
        secs += r.durationSecs;
        steps += r.steps.size();
        ++n;
    }
    if (usedRuns) *usedRuns = n;
    return steps ? static_cast<double>(secs) / steps : 0.0;
}

int priorityRank(Priority p) { return p == Priority::Alta ? 0 : p == Priority::Media ? 1 : 2; }
} // namespace

PlanStore::PlanStore(std::shared_ptr<ITestCaseRepository> repo, TestCaseStore& cases, RunHistoryStore& history, QObject* parent)
    : QObject(parent), m_repo(std::move(repo)), m_cases(cases), m_history(history) {
    connect(&m_cases, &TestCaseStore::casesChanged, this, [this]() {
        // Los casos borrados salen de todos los planes.
        bool changed = false;
        for (auto& p : m_plans) {
            const int before = p.caseIds.size();
            p.caseIds.erase(std::remove_if(p.caseIds.begin(), p.caseIds.end(), [this](const QString& id) { return !m_cases.find(id); }), p.caseIds.end());
            changed = changed || p.caseIds.size() != before;
        }
        if (changed) persist();
        else emit planChanged();
    });
    connect(&m_history, &RunHistoryStore::historyChanged, this, &PlanStore::planChanged);
}

void PlanStore::load() {
    auto loaded = m_repo ? m_repo->loadPlans() : std::nullopt;
    if (loaded && !loaded->plans.isEmpty()) {
        m_plans = loaded->plans;
        m_activeId = loaded->activeId;
    } else {
        TestPlan p;
        p.id = QStringLiteral("PL-0001");
        p.caseIds = seed::defaultPlanIds();
        p.createdAt = QDateTime::currentDateTime();
        m_plans = {p};
        m_activeId = p.id;
    }
    if (!find(m_activeId)) {
        m_activeId.clear();
        for (const auto& p : m_plans) if (!p.archived) { m_activeId = p.id; break; }
        if (m_activeId.isEmpty()) m_activeId = m_plans.first().id;
    }
    emit plansChanged();
    emit planChanged();
}

const TestPlan* PlanStore::find(const QString& id) const {
    auto it = std::find_if(m_plans.cbegin(), m_plans.cend(), [&](const TestPlan& p) { return p.id == id; });
    return it == m_plans.cend() ? nullptr : &*it;
}

TestPlan* PlanStore::activePlan() {
    auto it = std::find_if(m_plans.begin(), m_plans.end(), [&](const TestPlan& p) { return p.id == m_activeId; });
    return it == m_plans.end() ? nullptr : &*it;
}

void PlanStore::setActive(const QString& id) {
    if (m_activeId == id || !find(id)) return;
    m_activeId = id;
    persist();
}

QString PlanStore::nextId() const {
    int maxNum = 0;
    for (const auto& p : m_plans) {
        bool ok = false;
        const int n = p.id.mid(3).toInt(&ok);
        if (ok) maxNum = std::max(maxNum, n);
    }
    return QStringLiteral("PL-%1").arg(maxNum + 1, 4, 10, QLatin1Char('0'));
}

QString PlanStore::createPlan(const QString& name) {
    TestPlan p;
    p.id = nextId();
    p.name = name.trimmed().isEmpty() ? tr("Plan sin nombre") : name.trimmed();
    p.createdAt = QDateTime::currentDateTime();
    m_plans.append(p);
    m_activeId = p.id;
    persist();
    emit plansChanged();
    return p.id;
}

QString PlanStore::duplicatePlan(const QString& id) {
    const TestPlan* src = find(id);
    if (!src) return {};
    TestPlan p = *src;
    p.id = nextId();
    p.name = src->name + tr(" (copia)");
    p.archived = false;
    p.createdAt = QDateTime::currentDateTime();
    m_plans.insert(static_cast<int>(src - m_plans.constData()) + 1, p);
    m_activeId = p.id;
    persist();
    emit plansChanged();
    return p.id;
}

void PlanStore::setArchived(const QString& id, bool archived) {
    for (auto& p : m_plans) {
        if (p.id != id || p.archived == archived) continue;
        p.archived = archived;
        persist();
        emit plansChanged();
        return;
    }
}

void PlanStore::removePlan(const QString& id) {
    const TestPlan* p = find(id);
    if (!p) return;
    const int pos = static_cast<int>(p - m_plans.constData());
    m_plans.removeAt(pos);
    if (m_plans.isEmpty()) {
        TestPlan fresh;
        fresh.id = QStringLiteral("PL-0001");
        fresh.name = tr("Nuevo plan");
        fresh.createdAt = QDateTime::currentDateTime();
        m_plans.append(fresh);
    }
    if (m_activeId == id) m_activeId = m_plans[std::min(pos, static_cast<int>(m_plans.size()) - 1)].id;
    persist();
    emit plansChanged();
}

// ---- Contenido del plan activo -------------------------------------------------------------

void PlanStore::setName(const QString& name) {
    if (auto* p = activePlan()) { p->name = name; persist(); emit plansChanged(); }
}

void PlanStore::toggle(const QString& caseId) {
    auto* p = activePlan();
    if (!p) return;
    if (p->contains(caseId)) p->caseIds.removeAll(caseId);
    else p->caseIds.append(caseId);
    persist();
}

void PlanStore::moveCase(const QString& caseId, int delta) {
    auto* p = activePlan();
    if (!p) return;
    // Se mueve dentro de la lista ordenada visible (sin obsoletos ni inexistentes) y se reescribe.
    QStringList ordered = orderedCaseIds();
    const int i = ordered.indexOf(caseId);
    const int j = i + delta;
    if (i < 0 || j < 0 || j >= ordered.size()) return;
    ordered.move(i, j);
    p->caseIds = ordered;
    persist();
}

void PlanStore::selectAll() {
    auto* p = activePlan();
    if (!p) return;
    for (const auto& c : m_cases.cases()) if (c.status != CaseStatus::Obsoleto && !p->contains(c.id)) p->caseIds << c.id;
    persist();
}

void PlanStore::selectNone() {
    if (auto* p = activePlan()) { p->caseIds.clear(); persist(); }
}

void PlanStore::selectHighPriority() {
    auto* p = activePlan();
    if (!p) return;
    p->caseIds.clear();
    for (const auto& c : m_cases.cases()) if (c.priority == Priority::Alta && c.status != CaseStatus::Obsoleto) p->caseIds << c.id;
    persist();
}

void PlanStore::sortByPriority() {
    auto* p = activePlan();
    if (!p) return;
    QStringList ordered = orderedCaseIds();
    std::stable_sort(ordered.begin(), ordered.end(), [this](const QString& a, const QString& b) {
        const TestCase* ca = m_cases.find(a);
        const TestCase* cb = m_cases.find(b);
        return priorityRank(ca ? ca->priority : Priority::Baja) < priorityRank(cb ? cb->priority : Priority::Baja);
    });
    p->caseIds = ordered;
    persist();
}

QStringList PlanStore::orderedCaseIds(const QString& planId) const {
    const TestPlan* p = find(planId);
    if (!p) return {};
    QStringList out;
    for (const auto& id : p->caseIds) {
        const TestCase* c = m_cases.find(id);
        if (c && c->status != CaseStatus::Obsoleto) out << id;
    }
    return out;
}

int PlanStore::totalSteps() const {
    int n = 0;
    for (const auto& id : orderedCaseIds()) if (const auto* c = m_cases.find(id)) n += c->steps.size();
    return n;
}

int PlanStore::estimatedSecs() const {
    const double globalPerStep = averageSecsPerStep(m_history.runs());
    double total = 0;
    for (const auto& id : orderedCaseIds()) {
        const TestCase* c = m_cases.find(id);
        if (!c) continue;
        const double own = averageSecsPerStep(m_history.runsForCase(id));
        const double perStep = own > 0 ? own : globalPerStep > 0 ? globalPerStep : kDefaultSecsPerStep;
        total += perStep * c->steps.size();
    }
    return static_cast<int>(total + 0.5);
}

QString PlanStore::estimatedTime() const {
    const int mins = (estimatedSecs() + 30) / 60;
    if (mins >= 60) return tr("%1 h %2 min").arg(mins / 60).arg(mins % 60);
    return tr("%1 min").arg(mins);
}

QString PlanStore::estimateBasis() const {
    int used = 0;
    averageSecsPerStep(m_history.runs(), &used);
    if (used == 0) return tr("3 min por paso · sin historial");
    return used == 1 ? tr("según 1 ejecución") : tr("según %1 ejecuciones").arg(used);
}

// ---- Ciclos --------------------------------------------------------------------------------

std::optional<PlanReport> PlanStore::latestCycle(const QString& planId) const {
    const PlanRun* latest = nullptr;
    for (const auto& r : m_history.plans())
        if (r.planId == planId && (!latest || r.startedAt >= latest->startedAt)) latest = &r;
    if (!latest) return std::nullopt;
    return m_history.report(latest->id);
}

QList<PlanReport> PlanStore::cycles(const QString& planId) const {
    QList<const PlanRun*> runs;
    for (const auto& r : m_history.plans()) if (r.planId == planId) runs.append(&r);
    // Del más reciente al más antiguo; a igual inicio, el de id mayor (se creó después).
    std::stable_sort(runs.begin(), runs.end(), [](const PlanRun* a, const PlanRun* b) {
        return a->startedAt != b->startedAt ? a->startedAt > b->startedAt : a->id > b->id;
    });
    QList<PlanReport> out;
    out.reserve(runs.size());
    for (const PlanRun* r : runs) out.append(m_history.report(r->id));
    return out;
}

int PlanStore::cycleCount(const QString& planId) const {
    int n = 0;
    for (const auto& r : m_history.plans()) if (r.planId == planId) ++n;
    return n;
}

bool PlanStore::save() {
    if (!m_repo) return false;
    if (m_repo->savePlans(PlanCollection{m_activeId, m_plans})) return true;
    emit saveFailed(tr("los planes"));
    return false;
}

void PlanStore::persist() {
    save();
    emit planChanged();
}

} // namespace qaflow
