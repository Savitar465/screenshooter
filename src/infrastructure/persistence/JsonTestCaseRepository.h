#pragma once

#include "core/services/ITestCaseRepository.h"

#include <QString>

namespace qaflow {

/// Guarda casos y plan como JSON en el directorio de datos de la aplicación.
class JsonTestCaseRepository : public ITestCaseRepository {
public:
    explicit JsonTestCaseRepository(const QString& dataDir);

    std::optional<QList<TestCase>> loadCases() override;
    bool saveCases(const QList<TestCase>& cases) override;
    std::optional<TestPlan> loadPlan() override;
    bool savePlan(const TestPlan& plan) override;

    QString casesPath() const { return m_casesPath; }

private:
    QString m_casesPath;
    QString m_planPath;
};

} // namespace qaflow
