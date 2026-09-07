#pragma once

#include "core/models/TestCase.h"
#include "core/models/TestPlan.h"

#include <QList>
#include <optional>

namespace qaflow {

/// Persistencia de casos de prueba y de los planes. La implementación concreta vive en infrastructure/.
class ITestCaseRepository {
public:
    virtual ~ITestCaseRepository() = default;

    virtual std::optional<QList<TestCase>> loadCases() = 0;
    virtual bool saveCases(const QList<TestCase>& cases) = 0;

    virtual std::optional<PlanCollection> loadPlans() = 0;
    virtual bool savePlans(const PlanCollection& plans) = 0;
};

} // namespace qaflow
