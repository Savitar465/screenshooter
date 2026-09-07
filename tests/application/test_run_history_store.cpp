// RunHistoryStore (application/RunHistoryStore.h): historial de ejecuciones y planes.

#include "support/AppFixture.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;

class RunHistoryStoreTest : public QObject {
    Q_OBJECT
private slots:
    void addRunAssignsSequentialIdsAndPersists() {
        AppFixture f;
        RunRecord r;
        r.caseId = QStringLiteral("TC-101");
        QCOMPARE(f.history.addRun(r).id, QStringLiteral("R-0001"));
        QCOMPARE(f.history.addRun(r).id, QStringLiteral("R-0002"));
        QCOMPARE(f.historyRepo->saves, 2);
        QCOMPARE(f.history.runsForCase(QStringLiteral("TC-101")).size(), 2);
        QCOMPARE(f.history.runsForCase(QStringLiteral("TC-101")).first().id, QStringLiteral("R-0002"));   // la más reciente primero
        QVERIFY(f.history.findRun(QStringLiteral("R-0002")));
        QVERIFY(!f.history.findRun(QStringLiteral("R-0009")));
    }

    void planRunGroupsRecordsAndProducesReport() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"));
        const QString planRunId = f.run.planRunId();
        f.run.mark(StepResult::Pass);
        f.run.finish();
        f.run.mark(StepResult::Fail);
        f.run.finish();

        QCOMPARE(f.history.runsForPlan(planRunId).size(), 2);
        const PlanReport rep = f.history.report(planRunId);
        QCOMPARE(rep.plan.name, QStringLiteral("Regresión"));
        QCOMPARE(rep.total(), 2);
        QCOMPARE(rep.executed, 2);
        QCOMPARE(rep.passed, 1);
        QCOMPARE(rep.failed, 1);
        QCOMPARE(rep.successRate(), 50);
        QCOMPARE(static_cast<int>(rep.verdict()), static_cast<int>(Verdict::Fallido));
        QCOMPARE(rep.rows[0].caseId, QStringLiteral("TC-103"));
        QVERIFY(rep.toMarkdown().contains(QStringLiteral("| TC-107 |")));
    }

    void reportResolvesPendingTitlesFromCurrentCases() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107"), QStringLiteral("TC-102")}, QStringLiteral("Parcial"));
        const QString planRunId = f.run.planRunId();
        f.run.mark(StepResult::Pass);
        f.run.finish();
        f.run.abandon();

        const PlanReport rep = f.history.report(planRunId);
        QVERIFY(rep.plan.isFinished());
        QCOMPARE(rep.executed, 1);
        QCOMPARE(rep.pending(), 2);
        QVERIFY(!rep.rows[1].executed);
        QCOMPARE(rep.rows[2].title, f.store.find(QStringLiteral("TC-102"))->title);
        QCOMPARE(f.history.report(QStringLiteral("PR-0099")).total(), 0);   // plan inexistente: informe vacío
    }

    void finishPlanIsIdempotent() {
        AppFixture f;
        const QString id = f.history.startPlan(QStringLiteral("Uno"), {QStringLiteral("TC-103")});
        QVERIFY(f.history.startPlan(QStringLiteral("Vacío"), {}).isEmpty());
        f.history.finishPlan(id);
        const QDateTime first = f.history.findPlan(id)->finishedAt;
        f.history.finishPlan(id);
        QCOMPARE(f.history.findPlan(id)->finishedAt, first);
    }

    void historyRoundTripsThroughRepositoryAndIdsContinue() {
        AppFixture f;
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
        QCOMPARE(again.runs().last().id, QStringLiteral("R-0002"));
    }
};

QTEST_APPLESS_MAIN(RunHistoryStoreTest)
#include "test_run_history_store.moc"
