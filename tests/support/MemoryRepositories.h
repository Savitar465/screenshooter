#pragma once

// Repositorios en memoria: implementan las interfaces de core/services/ sin tocar el disco.
// Guardan lo último escrito para poder comprobarlo y cuentan las escrituras.

#include "core/services/IBugRepository.h"
#include "core/services/IRunHistoryRepository.h"
#include "core/services/IRunSessionRepository.h"
#include "core/services/ISecretStore.h"
#include "core/services/ISettingsRepository.h"
#include "core/services/ITestCaseRepository.h"

#include <QMap>

#include <optional>

namespace qaflow::testing {

class MemoryTestCaseRepository : public ITestCaseRepository {
public:
    std::optional<QList<TestCase>> cases;
    std::optional<PlanCollection> plans;
    int caseSaves = 0;
    int planSaves = 0;
    bool failWrites = false;   // simula un disco que no acepta escrituras

    std::optional<QList<TestCase>> loadCases() override { return cases; }
    bool saveCases(const QList<TestCase>& c) override { if (failWrites) return false; cases = c; ++caseSaves; return true; }
    std::optional<PlanCollection> loadPlans() override { return plans; }
    bool savePlans(const PlanCollection& p) override { if (failWrites) return false; plans = p; ++planSaves; return true; }
};

class MemoryRunHistoryRepository : public IRunHistoryRepository {
public:
    std::optional<RunHistory> history;
    int saves = 0;
    bool failWrites = false;

    std::optional<RunHistory> loadHistory() override { return history; }
    bool saveHistory(const RunHistory& h) override { if (failWrites) return false; history = h; ++saves; return true; }
};

class MemoryRunSessionRepository : public IRunSessionRepository {
public:
    std::optional<RunSession> session;
    bool failWrites = false;

    std::optional<RunSession> loadSession() override { return session; }
    bool saveSession(const RunSession& s) override { if (failWrites) return false; session = s; return true; }
    void clearSession() override { session.reset(); }
};

class MemoryBugRepository : public IBugRepository {
public:
    std::optional<BugLedger> ledger;
    int saves = 0;
    bool failWrites = false;

    std::optional<BugLedger> loadLedger() override { return ledger; }
    bool saveLedger(const BugLedger& l) override { if (failWrites) return false; ledger = l; ++saves; return true; }
};

class MemorySettingsRepository : public ISettingsRepository {
public:
    TrackerSettings tracker;
    CaptureSettings capture;
    AppSettings app;

    TrackerSettings loadTracker() override { return tracker; }
    void saveTracker(const TrackerSettings& s) override { tracker = s; }
    CaptureSettings loadCapture() override { return capture; }
    void saveCapture(const CaptureSettings& c) override { capture = c; }
    AppSettings loadApp() override { return app; }
    void saveApp(const AppSettings& a) override { app = a; }
};

class MemorySecretStore : public ISecretStore {
public:
    QMap<QString, QString> values;

    std::optional<QString> read(const QString& key) override { return values.contains(key) ? std::optional<QString>(values.value(key)) : std::nullopt; }
    bool write(const QString& key, const QString& value) override { values[key] = value; return true; }
    void remove(const QString& key) override { values.remove(key); }
    QString description() const override { return QStringLiteral("memoria"); }
    bool isSecure() const override { return true; }
};

} // namespace qaflow::testing
