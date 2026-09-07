#pragma once

#include "core/models/TestPlan.h"
#include "core/services/ITestCaseRepository.h"

#include <QObject>
#include <memory>

namespace qaflow {

class TestCaseStore;

class PlanStore : public QObject {
    Q_OBJECT
public:
    PlanStore(std::shared_ptr<ITestCaseRepository> repo, TestCaseStore& cases, QObject* parent = nullptr);

    void load();
    const TestPlan& plan() const { return m_plan; }

    void setName(const QString& name);
    void toggle(const QString& caseId);
    void selectAll();
    void selectNone();
    void selectHighPriority();

    /// Casos del plan en el orden de la lista de casos (ignora ids inexistentes u obsoletos).
    QStringList orderedCaseIds() const;
    int totalSteps() const;
    QString estimatedTime() const; // 3 min por paso

signals:
    void planChanged();

private:
    void persist();

    std::shared_ptr<ITestCaseRepository> m_repo;
    TestCaseStore& m_cases;
    TestPlan m_plan;
};

} // namespace qaflow
