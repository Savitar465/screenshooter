#include "application/PlanStore.h"
#include "application/RunController.h"
#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"

#include <QtTest>

using namespace qaflow;

namespace {
/// Repositorio en memoria para aislar los tests del disco.
class MemoryRepo : public ITestCaseRepository {
public:
    std::optional<QList<TestCase>> cases;
    std::optional<TestPlan> plan;
    int saves = 0;
    std::optional<QList<TestCase>> loadCases() override { return cases; }
    bool saveCases(const QList<TestCase>& c) override { cases = c; ++saves; return true; }
    std::optional<TestPlan> loadPlan() override { return plan; }
    bool savePlan(const TestPlan& p) override { plan = p; return true; }
};

class MemoryHistoryRepo : public IRunHistoryRepository {
public:
    std::optional<RunHistory> history;
    int saves = 0;
    std::optional<RunHistory> loadHistory() override { return history; }
    bool saveHistory(const RunHistory& h) override { history = h; ++saves; return true; }
};

/// Todo lo necesario para ejecutar casos en memoria.
struct Fixture {
    std::shared_ptr<MemoryRepo> repo = std::make_shared<MemoryRepo>();
    std::shared_ptr<MemoryHistoryRepo> historyRepo = std::make_shared<MemoryHistoryRepo>();
    TestCaseStore store{repo};
    RunHistoryStore history{historyRepo, store};
    RunController run{store, history};
    Fixture() { store.load(); history.load(); }
};
} // namespace

class StoresTest : public QObject {
    Q_OBJECT
private slots:
    void loadsSeedWhenEmpty() {
        auto repo = std::make_shared<MemoryRepo>();
        TestCaseStore store(repo);
        store.load();
        QCOMPARE(store.cases().size(), 7);
        QCOMPARE(store.selectedId(), QStringLiteral("TC-104"));
    }

    void createCaseAssignsNextId() {
        auto repo = std::make_shared<MemoryRepo>();
        TestCaseStore store(repo);
        store.load();
        const QString id = store.createCase();
        QCOMPARE(id, QStringLiteral("TC-108"));
        QCOMPARE(store.selectedId(), id);
        QVERIFY(store.save());
        QVERIFY(repo->saves > 0);
    }

    void removingStepReassignsShots() {
        auto repo = std::make_shared<MemoryRepo>();
        TestCaseStore store(repo);
        store.load();
        const QString id = QStringLiteral("TC-104"); // 4 pasos
        store.addShot(id, Screenshot{1, 2, QStringLiteral("a.png"), {}});
        store.addShot(id, Screenshot{2, 4, QStringLiteral("b.png"), {}});
        store.removeStep(id, 1); // borra el paso 2
        const TestCase* c = store.find(id);
        QCOMPARE(c->steps.size(), 3);
        QCOMPARE(c->shots[0].step, 0); // quedó sin asignar
        QCOMPARE(c->shots[1].step, 3); // se desplazó
    }

    void runFlowRecordsOutcome() {
        Fixture f;
        TestCaseStore& store = f.store;
        RunController& run = f.run;
        run.start(QStringLiteral("TC-102")); // 2 pasos
        QVERIFY(run.isRunning());
        run.mark(StepResult::Pass);
        QCOMPARE(run.state().idx, 1);
        run.setNote(QStringLiteral("se rompió"));
        run.mark(StepResult::Fail);
        QVERIFY(run.state().finished);
        QCOMPARE(run.state().results[1].note, QStringLiteral("se rompió"));
        QVERIFY(!run.finish());
        QCOMPARE(static_cast<int>(store.find(QStringLiteral("TC-102"))->lastRun.outcome), static_cast<int>(RunOutcome::Failed));
    }

    void blockFinishesEarly() {
        Fixture f;
        TestCaseStore& store = f.store;
        RunController& run = f.run;
        run.start(QStringLiteral("TC-104"));
        run.mark(StepResult::Block);
        QVERIFY(run.state().finished);
        QCOMPARE(static_cast<int>(run.state().verdict()), static_cast<int>(Verdict::Bloqueado));
    }

    void sequenceAdvancesThroughPlan() {
        Fixture f;
        TestCaseStore& store = f.store;
        RunController& run = f.run;
        run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}); // 1 paso cada uno
        QCOMPARE(run.state().caseId, QStringLiteral("TC-103"));
        run.mark(StepResult::Pass);
        QVERIFY(run.finish());
        QCOMPARE(run.state().caseId, QStringLiteral("TC-107"));
        QCOMPARE(store.selectedId(), QStringLiteral("TC-107"));
        run.mark(StepResult::Pass);
        QVERIFY(!run.finish());
        QVERIFY(!run.isRunning());
    }

    void finishedRunIsArchived() {
        Fixture f;
        f.run.start(QStringLiteral("TC-102")); // 2 pasos
        f.run.mark(StepResult::Pass);
        f.run.setNote(QStringLiteral("se rompió"));
        f.run.mark(StepResult::Fail);
        QCOMPARE(f.history.runs().size(), 0); // aún no se ha cerrado
        f.run.finish();
        QCOMPARE(f.history.runs().size(), 1);
        const RunRecord& r = f.history.runs().first();
        QCOMPARE(r.id, QStringLiteral("R-0001"));
        QCOMPARE(r.caseId, QStringLiteral("TC-102"));
        QVERIFY(r.planRunId.isEmpty());
        QCOMPARE(static_cast<int>(r.verdict), static_cast<int>(Verdict::Fallido));
        QCOMPARE(r.steps.size(), 2);
        QCOMPARE(r.plannedSteps, 2);
        QCOMPARE(r.steps[1].note, QStringLiteral("se rompió"));
        QVERIFY(!r.steps[0].action.isEmpty()); // instantánea del texto del paso
        QVERIFY(f.historyRepo->saves > 0);
        QCOMPARE(f.history.runsForCase(QStringLiteral("TC-102")).size(), 1);
    }

    void startingAnotherCaseArchivesTheFinishedOne() {
        Fixture f;
        f.run.start(QStringLiteral("TC-103")); // 1 paso
        f.run.mark(StepResult::Pass);
        f.run.start(QStringLiteral("TC-107"));  // sin pulsar "Finalizar"
        QCOMPARE(f.history.runs().size(), 1);
        QCOMPARE(static_cast<int>(f.store.find(QStringLiteral("TC-103"))->lastRun.outcome), static_cast<int>(RunOutcome::Passed));
    }

    void restartArchivesEachAttempt() {
        Fixture f;
        f.run.start(QStringLiteral("TC-103"));
        f.run.mark(StepResult::Fail);
        f.run.restart();
        f.run.mark(StepResult::Pass);
        f.run.finish();
        const auto runs = f.history.runsForCase(QStringLiteral("TC-103"));
        QCOMPARE(runs.size(), 2);
        QCOMPARE(static_cast<int>(runs[0].verdict), static_cast<int>(Verdict::Superado)); // la más reciente primero
        QCOMPARE(static_cast<int>(runs[1].verdict), static_cast<int>(Verdict::Fallido));
    }

    void blockedOutcomeIsRecordedOnCase() {
        Fixture f;
        f.run.start(QStringLiteral("TC-104"));
        f.run.mark(StepResult::Block);
        f.run.finish();
        QCOMPARE(static_cast<int>(f.store.find(QStringLiteral("TC-104"))->lastRun.outcome), static_cast<int>(RunOutcome::Blocked));
        QCOMPARE(f.history.runs().first().steps.size(), 1);
        QCOMPARE(f.history.runs().first().plannedSteps, 4);
    }

    void planRunGroupsRecordsAndProducesReport() {
        Fixture f;
        QSignalSpy completed(&f.run, &RunController::planCompleted);
        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"));
        const QString planId = f.run.planRunId();
        QCOMPARE(planId, QStringLiteral("PR-0001"));
        QVERIFY(!f.history.findPlan(planId)->isFinished());

        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.finish());       // pasa al siguiente
        QCOMPARE(completed.count(), 0);
        f.run.mark(StepResult::Fail);
        QVERIFY(!f.run.finish());      // fin del plan
        QCOMPARE(completed.count(), 1);
        QVERIFY(f.run.planRunId().isEmpty());
        QVERIFY(f.history.findPlan(planId)->isFinished());

        const PlanReport rep = f.history.report(planId);
        QCOMPARE(rep.plan.name, QStringLiteral("Regresión"));
        QCOMPARE(rep.total(), 2);
        QCOMPARE(rep.executed, 2);
        QCOMPARE(rep.passed, 1);
        QCOMPARE(rep.failed, 1);
        QCOMPARE(rep.pending(), 0);
        QCOMPARE(rep.successRate(), 50);
        QCOMPARE(static_cast<int>(rep.verdict()), static_cast<int>(Verdict::Fallido));
        QCOMPARE(rep.rows[0].caseId, QStringLiteral("TC-103"));
        QVERIFY(rep.rows[0].executed);
        QCOMPARE(f.history.runsForPlan(planId).size(), 2);
        QVERIFY(rep.toMarkdown().contains(QStringLiteral("| TC-107 |")));
    }

    void abandonedPlanKeepsPendingCases() {
        Fixture f;
        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107"), QStringLiteral("TC-102")}, QStringLiteral("Parcial"));
        const QString planId = f.run.planRunId();
        f.run.mark(StepResult::Pass);
        f.run.finish();
        f.run.abandon();
        const PlanReport rep = f.history.report(planId);
        QVERIFY(rep.plan.isFinished());
        QCOMPARE(rep.executed, 1);
        QCOMPARE(rep.pending(), 2);
        QVERIFY(!rep.rows[1].executed);
        QCOMPARE(rep.rows[2].title, f.store.find(QStringLiteral("TC-102"))->title); // título resuelto desde los casos
    }

    void historyRoundTripsThroughRepository() {
        Fixture f;
        f.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Uno"));
        f.run.mark(StepResult::Pass);
        f.run.finish();
        // Otra sesión que carga el mismo repositorio.
        RunHistoryStore again(f.historyRepo, f.store);
        again.load();
        QCOMPARE(again.runs().size(), 1);
        QCOMPARE(again.plans().size(), 1);
        RunController run2(f.store, again);
        run2.start(QStringLiteral("TC-107"));
        run2.mark(StepResult::Pass);
        run2.finish();
        QCOMPARE(again.runs().last().id, QStringLiteral("R-0002")); // los ids continúan
    }

    void planEstimate() {
        auto repo = std::make_shared<MemoryRepo>();
        TestCaseStore store(repo);
        store.load();
        PlanStore plan(repo, store);
        plan.load();
        QCOMPARE(plan.orderedCaseIds().size(), 4);
        QCOMPARE(plan.totalSteps(), 11);
        QCOMPARE(plan.estimatedTime(), QStringLiteral("33 min"));
        plan.selectHighPriority();
        QCOMPARE(plan.orderedCaseIds(), (QStringList{QStringLiteral("TC-101"), QStringLiteral("TC-102"), QStringLiteral("TC-104")}));
    }
};

QTEST_APPLESS_MAIN(StoresTest)
#include "test_stores.moc"
