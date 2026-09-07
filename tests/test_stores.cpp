#include "application/PlanStore.h"
#include "application/RunController.h"
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
        auto repo = std::make_shared<MemoryRepo>();
        TestCaseStore store(repo);
        store.load();
        RunController run(store);
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
        auto repo = std::make_shared<MemoryRepo>();
        TestCaseStore store(repo);
        store.load();
        RunController run(store);
        run.start(QStringLiteral("TC-104"));
        run.mark(StepResult::Block);
        QVERIFY(run.state().finished);
        QCOMPARE(static_cast<int>(run.state().verdict()), static_cast<int>(Verdict::Bloqueado));
    }

    void sequenceAdvancesThroughPlan() {
        auto repo = std::make_shared<MemoryRepo>();
        TestCaseStore store(repo);
        store.load();
        RunController run(store);
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
