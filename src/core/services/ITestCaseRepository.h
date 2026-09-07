#pragma once

#include "core/models/TestCase.h"
#include "core/models/TestPlan.h"

#include <QList>
#include <optional>

namespace qaflow {

/// Persistencia de casos de prueba y del plan. La implementación concreta vive en infrastructure/.
class ITestCaseRepository {
public:
    virtual ~ITestCaseRepository() = default;

    virtual std::optional<QList<TestCase>> loadCases() = 0;
    virtual bool saveCases(const QList<TestCase>& cases) = 0;

    virtual std::optional<TestPlan> loadPlan() = 0;
    virtual bool savePlan(const TestPlan& plan) = 0;
};

} // namespace qaflow
