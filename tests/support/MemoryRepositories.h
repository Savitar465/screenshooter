#pragma once

// Repositorios en memoria: implementan las interfaces de core/services/ sin tocar el disco.
// Guardan lo último escrito para poder comprobarlo y cuentan las escrituras.

#include "core/services/IRunHistoryRepository.h"
#include "core/services/IRunSessionRepository.h"
#include "core/services/ITestCaseRepository.h"

#include <optional>

namespace qaflow::testing {

class MemoryTestCaseRepository : public ITestCaseRepository {
public:
    std::optional<QList<TestCase>> cases;
    std::optional<PlanCollection> plans;
    int caseSaves = 0;
    int planSaves = 0;

    std::optional<QList<TestCase>> loadCases() override { return cases; }
    bool saveCases(const QList<TestCase>& c) override { cases = c; ++caseSaves; return true; }
    std::optional<PlanCollection> loadPlans() override { return plans; }
    bool savePlans(const PlanCollection& p) override { plans = p; ++planSaves; return true; }
};

class MemoryRunHistoryRepository : public IRunHistoryRepository {
public:
    std::optional<RunHistory> history;
    int saves = 0;

    std::optional<RunHistory> loadHistory() override { return history; }
    bool saveHistory(const RunHistory& h) override { history = h; ++saves; return true; }
};

class MemoryRunSessionRepository : public IRunSessionRepository {
public:
    std::optional<RunSession> session;

    std::optional<RunSession> loadSession() override { return session; }
    bool saveSession(const RunSession& s) override { session = s; return true; }
    void clearSession() override { session.reset(); }
};

} // namespace qaflow::testing
