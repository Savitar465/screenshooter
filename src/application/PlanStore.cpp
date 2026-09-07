#include "PlanStore.h"

#include "application/SeedData.h"
#include "application/TestCaseStore.h"

namespace qaflow {

PlanStore::PlanStore(std::shared_ptr<ITestCaseRepository> repo, TestCaseStore& cases, QObject* parent)
    : QObject(parent), m_repo(std::move(repo)), m_cases(cases) {
    connect(&m_cases, &TestCaseStore::casesChanged, this, &PlanStore::planChanged);
}

void PlanStore::load() {
    auto loaded = m_repo ? m_repo->loadPlan() : std::nullopt;
    if (loaded) m_plan = *loaded;
    else m_plan.caseIds = seed::defaultPlanIds();
    emit planChanged();
}

void PlanStore::setName(const QString& name) { m_plan.name = name; persist(); }

void PlanStore::toggle(const QString& caseId) {
    if (m_plan.caseIds.contains(caseId)) m_plan.caseIds.removeAll(caseId);
    else m_plan.caseIds.append(caseId);
    persist();
}

void PlanStore::selectAll() {
    m_plan.caseIds.clear();
    for (const auto& c : m_cases.cases()) if (c.status != CaseStatus::Obsoleto) m_plan.caseIds << c.id;
    persist();
}

void PlanStore::selectNone() { m_plan.caseIds.clear(); persist(); }

void PlanStore::selectHighPriority() {
    m_plan.caseIds.clear();
    for (const auto& c : m_cases.cases()) if (c.priority == Priority::Alta && c.status != CaseStatus::Obsoleto) m_plan.caseIds << c.id;
    persist();
}

QStringList PlanStore::orderedCaseIds() const {
    QStringList out;
    for (const auto& c : m_cases.cases()) if (c.status != CaseStatus::Obsoleto && m_plan.contains(c.id)) out << c.id;
    return out;
}

int PlanStore::totalSteps() const {
    int n = 0;
    for (const auto& id : orderedCaseIds()) if (const auto* c = m_cases.find(id)) n += c->steps.size();
    return n;
}

QString PlanStore::estimatedTime() const {
    const int mins = totalSteps() * 3;
    if (mins >= 60) return QStringLiteral("%1 h %2 min").arg(mins / 60).arg(mins % 60);
    return QStringLiteral("%1 min").arg(mins);
}

void PlanStore::persist() {
    if (m_repo) m_repo->savePlan(m_plan);
    emit planChanged();
}

} // namespace qaflow
