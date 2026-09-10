// PlanStore (application/PlanStore.h): colección de planes, orden de ejecución, ciclos y estimación.

#include "support/AppFixture.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;

class PlanStoreTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Colección ---------------------------------------------------------------------

    void loadsDefaultPlanWhenRepositoryIsEmpty() {
        AppFixture f;
        QCOMPARE(f.plans.plans().size(), 1);
        QCOMPARE(f.plans.activeId(), QStringLiteral("PL-0001"));
        QCOMPARE(f.plans.active()->name, QStringLiteral("Regresión Sprint 14"));
        QCOMPARE(f.plans.orderedCaseIds().size(), 4);
        QCOMPARE(f.plans.totalSteps(), 11);
    }

    void createDuplicateArchiveAndRemovePlans() {
        AppFixture f;
        QSignalSpy changed(&f.plans, &PlanStore::plansChanged);
        const QString id = f.plans.createPlan(QStringLiteral("Smoke"));
        QCOMPARE(id, QStringLiteral("PL-0002"));
        QCOMPARE(f.plans.activeId(), id);   // el nuevo pasa a ser el activo
        QVERIFY(f.plans.orderedCaseIds().isEmpty());
        QVERIFY(changed.count() >= 1);
        f.plans.toggle(QStringLiteral("TC-103"));

        const QString copy = f.plans.duplicatePlan(id);
        QCOMPARE(f.plans.active()->name, QStringLiteral("Smoke (copia)"));
        QCOMPARE(f.plans.orderedCaseIds(), QStringList{QStringLiteral("TC-103")});

        f.plans.setArchived(copy, true);
        QVERIFY(f.plans.find(copy)->archived);
        f.plans.removePlan(copy);
        QVERIFY(!f.plans.find(copy));
        QVERIFY(f.plans.find(f.plans.activeId()));   // el activo sigue siendo válido
    }

    void removingTheLastPlanLeavesAFreshOne() {
        AppFixture f;
        f.plans.removePlan(QStringLiteral("PL-0001"));
        QCOMPARE(f.plans.plans().size(), 1);
        QVERIFY(f.plans.active());
        QVERIFY(f.plans.orderedCaseIds().isEmpty());
    }

    void collectionAndActivePlanPersistTogether() {
        AppFixture f;
        f.plans.createPlan(QStringLiteral("Smoke"));
        QVERIFY(f.repo->plans.has_value());
        QCOMPARE(f.repo->plans->plans.size(), 2);
        QCOMPARE(f.repo->plans->activeId, QStringLiteral("PL-0002"));

        PlanStore again(f.repo, f.store, f.history);
        again.load();
        QCOMPARE(again.plans().size(), 2);
        QCOMPARE(again.activeId(), QStringLiteral("PL-0002"));
    }

    // ---- Contenido y orden -------------------------------------------------------------

    void togglingAppendsAtTheEndAndKeepsInsertionOrder() {
        AppFixture f;
        f.plans.selectNone();
        f.plans.toggle(QStringLiteral("TC-105"));
        f.plans.toggle(QStringLiteral("TC-101"));
        f.plans.toggle(QStringLiteral("TC-104"));
        QCOMPARE(f.plans.orderedCaseIds(), (QStringList{QStringLiteral("TC-105"), QStringLiteral("TC-101"), QStringLiteral("TC-104")}));
        f.plans.toggle(QStringLiteral("TC-101"));   // quitar
        QCOMPARE(f.plans.orderedCaseIds(), (QStringList{QStringLiteral("TC-105"), QStringLiteral("TC-104")}));
    }

    void moveCaseAndSortByPriority() {
        AppFixture f;
        f.plans.selectNone();
        for (const auto& id : {"TC-105", "TC-101", "TC-104"}) f.plans.toggle(QLatin1String(id));
        f.plans.moveCase(QStringLiteral("TC-104"), -2);
        QCOMPARE(f.plans.orderedCaseIds(), (QStringList{QStringLiteral("TC-104"), QStringLiteral("TC-105"), QStringLiteral("TC-101")}));
        f.plans.moveCase(QStringLiteral("TC-104"), -1);   // en el extremo: sin efecto
        QCOMPARE(f.plans.orderedCaseIds().first(), QStringLiteral("TC-104"));
        f.plans.sortByPriority();   // TC-105 es Media; TC-104 y TC-101 Alta (orden estable)
        QCOMPARE(f.plans.orderedCaseIds(), (QStringList{QStringLiteral("TC-104"), QStringLiteral("TC-101"), QStringLiteral("TC-105")}));
    }

    void selectHighPriorityFollowsCaseListOrder() {
        AppFixture f;
        f.plans.selectHighPriority();
        QCOMPARE(f.plans.orderedCaseIds(), (QStringList{QStringLiteral("TC-101"), QStringLiteral("TC-102"), QStringLiteral("TC-104")}));
        f.plans.selectAll();
        QCOMPARE(f.plans.orderedCaseIds().size(), 7);
    }

    void obsoleteCasesStayInPlanButAreNotExecuted() {
        AppFixture f;
        f.store.updateCase(QStringLiteral("TC-101"), [](TestCase& c) { c.status = CaseStatus::Obsoleto; });
        QCOMPARE(f.plans.orderedCaseIds().size(), 3);
        QVERIFY(f.plans.active()->contains(QStringLiteral("TC-101")));
    }

    void deletedCasesLeaveAllPlans() {
        AppFixture f;
        QVERIFY(f.plans.active()->contains(QStringLiteral("TC-104")));
        f.store.removeCase(QStringLiteral("TC-104"));
        QVERIFY(!f.plans.active()->contains(QStringLiteral("TC-104")));
        QCOMPARE(f.plans.orderedCaseIds().size(), 3);
    }

    // ---- Ciclos ------------------------------------------------------------------------

    void latestCycleTracksTheRunInProgressAndThenTheResult() {
        AppFixture f;
        const QString planId = f.plans.activeId();
        QVERIFY(!f.plans.latestCycle(planId).has_value());
        QCOMPARE(f.plans.cycleCount(planId), 0);

        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, f.plans.active()->name, planId);
        auto cycle = f.plans.latestCycle(planId);
        QVERIFY(cycle.has_value());
        QVERIFY(!cycle->plan.isFinished());
        QCOMPARE(cycle->executed, 0);
        f.run.mark(StepResult::Pass);
        f.run.finish();
        QCOMPARE(f.plans.latestCycle(planId)->executed, 1);   // progreso del ciclo en curso
        f.run.mark(StepResult::Fail);
        f.run.finish();
        cycle = f.plans.latestCycle(planId);
        QVERIFY(cycle->plan.isFinished());
        QCOMPARE(cycle->successRate(), 50);
        QCOMPARE(f.plans.cycleCount(planId), 1);
    }

    void eachPlanHasItsOwnCycles() {
        AppFixture f;
        const QString planId = f.plans.activeId();
        f.run.startSequence({QStringLiteral("TC-103")}, f.plans.active()->name, planId);
        f.run.mark(StepResult::Pass);
        f.run.finish();
        f.run.startSequence({QStringLiteral("TC-103")}, f.plans.active()->name, planId);
        QCOMPARE(f.plans.cycleCount(planId), 2);
        QCOMPARE(f.plans.latestCycle(planId)->executed, 0);   // el segundo ciclo es el "último"
        const QString other = f.plans.createPlan(QStringLiteral("Otro"));
        QVERIFY(!f.plans.latestCycle(other).has_value());
        QCOMPARE(f.history.findPlan(f.run.planRunId())->planId, planId);
    }

    void cyclesListsEveryRunOfThePlanMostRecentFirst() {
        AppFixture f;
        const QString planId = f.plans.activeId();
        QVERIFY(f.plans.cycles(planId).isEmpty());

        f.run.startSequence({QStringLiteral("TC-103")}, f.plans.active()->name, planId);
        const QString first = f.run.planRunId();
        f.run.mark(StepResult::Pass);
        f.run.finish();
        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, f.plans.active()->name, planId);
        const QString second = f.run.planRunId();
        f.run.mark(StepResult::Fail);
        f.run.finish();

        const QList<PlanReport> cycles = f.plans.cycles(planId);
        QCOMPARE(cycles.size(), 2);
        QCOMPARE(cycles[0].plan.id, second);          // el más reciente primero, aunque siga en curso
        QVERIFY(!cycles[0].plan.isFinished());
        QCOMPARE(cycles[0].executed, 1);
        QCOMPARE(cycles[0].total(), 2);
        QCOMPARE(cycles[0].failed, 1);
        QCOMPARE(cycles[1].plan.id, first);
        QVERIFY(cycles[1].plan.isFinished());
        QCOMPARE(cycles[1].successRate(), 100);
        QCOMPARE(cycles[0].plan.id, f.plans.latestCycle(planId)->plan.id);

        // Los ciclos de otro plan no se mezclan
        const QString other = f.plans.createPlan(QStringLiteral("Otro"));
        QVERIFY(f.plans.cycles(other).isEmpty());
        QCOMPARE(f.plans.cycles(planId).size(), 2);
    }

    // ---- Estimación --------------------------------------------------------------------

    void estimateFallsBackToThreeMinutesPerStep() {
        AppFixture f;
        QCOMPARE(f.plans.estimatedSecs(), 11 * 180);
        QCOMPARE(f.plans.estimatedTime(), QStringLiteral("33 min"));
        QVERIFY(f.plans.estimateBasis().contains(QStringLiteral("sin historial")));
    }

    void estimateUsesRealDurationsWhenAvailable() {
        AppFixture f;
        RunRecord r;   // TC-104 (4 pasos) tardó 2 min → 30 s/paso
        r.caseId = QStringLiteral("TC-104");
        r.durationSecs = 120;
        for (int i = 0; i < 4; ++i) r.steps.append(RunRecordStep{{}, {}, StepResult::Pass, {}, 30});
        f.history.addRun(r);
        // TC-104 por su propia media (120 s); los otros 7 pasos por la media global (30 s) = 210 s.
        QCOMPARE(f.plans.estimatedSecs(), 330);
        QCOMPARE(f.plans.estimatedTime(), QStringLiteral("6 min"));
        QCOMPARE(f.plans.estimateBasis(), QStringLiteral("según 1 ejecución"));
    }
};

QTEST_APPLESS_MAIN(PlanStoreTest)
#include "test_plan_store.moc"
