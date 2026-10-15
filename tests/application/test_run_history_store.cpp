// RunHistoryStore (application/RunHistoryStore.h): historial de ejecuciones y planes, incluida la
// migración de las evidencias que antes eran del caso y ahora son de su ejecución.

#include "support/AppFixture.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;

class RunHistoryStoreTest : public QObject {
    Q_OBJECT
private slots:
    // El ciclo se queda con el ambiente en el que se prueba y con la ronda del control de calidad de
    // la que es: sin eso, dos ejecuciones del mismo plan son indistinguibles.
    void aPlanCycleRemembersItsEnvironmentAndItsRevision() {
        AppFixture f;
        QVERIFY(f.history.lastEnvironment().isEmpty());
        const QString id = f.history.startPlan(QStringLiteral("Regresión"), {QStringLiteral("TC-103")},
                                               QStringLiteral("PL-0001"), QStringLiteral("  QA  "));
        QVERIFY(!id.isEmpty());
        QCOMPARE(f.history.findPlan(id)->environment, QStringLiteral("QA"));   // sin espacios de sobra
        QCOMPARE(f.history.lastEnvironment(), QStringLiteral("QA"));
        QCOMPARE(f.history.findPlan(id)->revision, 0);

        f.history.noteCycleRevision(id, QStringLiteral("IS-0001"), 2);
        QCOMPARE(f.history.findPlan(id)->issueId, QStringLiteral("IS-0001"));
        QCOMPARE(f.history.findPlan(id)->revision, 2);

        // Sin issue no hay nada que anotar: un ciclo suelto se queda como estaba.
        const QString loose = f.history.startPlan(QStringLiteral("Suelto"), {QStringLiteral("TC-103")});
        f.history.noteCycleRevision(loose, QString(), 3);
        QVERIFY(f.history.findPlan(loose)->issueId.isEmpty());
        QCOMPARE(f.history.findPlan(loose)->revision, 0);
        QCOMPARE(f.history.lastEnvironment(), QStringLiteral("QA"));   // el suelto no dijo ambiente
    }

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

    // Datos anteriores: las evidencias sueltas del caso pasan a su última ejecución.
    void looseEvidenceIsAdoptedByTheLastRunOfItsCase() {
        AppFixture f;
        RunRecord vieja;
        vieja.caseId = QStringLiteral("TC-101");
        vieja.finishedAt = QDateTime(QDate(2026, 5, 10), QTime(9, 0));
        f.history.addRun(vieja);
        RunRecord reciente = vieja;
        reciente.finishedAt = QDateTime(QDate(2026, 5, 12), QTime(9, 0));
        const QString ultima = f.history.addRun(reciente).id;
        f.store.updateCase(QStringLiteral("TC-101"), [](TestCase& c) {
            c.shots = {Screenshot{1, 2, QStringLiteral("cap_001.png"), QStringLiteral("/x/cap_001.png")}};
        });
        // Y un caso que nunca se ejecutó: su evidencia no tiene ejecución donde enseñarse.
        f.store.updateCase(QStringLiteral("TC-106"), [](TestCase& c) {
            c.shots = {Screenshot{2, 0, QStringLiteral("cap_002.png"), QStringLiteral("/x/cap_002.png")}};
        });

        QCOMPARE(f.history.adoptLooseEvidence(), 2);
        QCOMPARE(f.store.find(QStringLiteral("TC-101"))->shots.first().runId, ultima);
        QCOMPARE(f.store.find(QStringLiteral("TC-101"))->shots.first().step, 2);   // conserva su paso
        QVERIFY(f.store.find(QStringLiteral("TC-106"))->shots.isEmpty());
        QCOMPARE(f.history.adoptLooseEvidence(), 0);   // ya no queda nada suelto
    }

    // La evidencia de la ejecución en curso todavía no tiene id: no se la queda la anterior.
    void theEvidenceOfTheRunningCaseIsLeftAlone() {
        AppFixture f;
        RunRecord anterior;
        anterior.caseId = QStringLiteral("TC-101");
        f.history.addRun(anterior);
        f.store.updateCase(QStringLiteral("TC-101"), [](TestCase& c) {
            c.shots = {Screenshot{1, 1, QStringLiteral("cap_001.png"), QStringLiteral("/x/cap_001.png")}};
        });
        QCOMPARE(f.history.adoptLooseEvidence(QStringLiteral("TC-101")), 0);
        QVERIFY(f.store.find(QStringLiteral("TC-101"))->shots.first().runId.isEmpty());
    }

    void removingAPlanRunDropsItsRunsAndEvidenceAndRefreshesTheLastRun() {
        AppFixture f;
        // TC-103 ya tiene una ejecución suelta superada: es la que debe quedar como "última".
        f.run.start(QStringLiteral("TC-103"));
        f.run.mark(StepResult::Pass);
        f.run.finish();
        const QDateTime looseAt = f.store.find(QStringLiteral("TC-103"))->lastRun.at;

        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"));
        const QString planRunId = f.run.planRunId();
        f.store.addShot(QStringLiteral("TC-103"), Screenshot{1, 1, QStringLiteral("cap_001.png"), QStringLiteral("/tmp/qaflow-test/cap_001.png"), {}});
        f.run.mark(StepResult::Fail);
        f.run.finish();
        f.run.mark(StepResult::Pass);
        f.run.finish();
        QCOMPARE(f.history.runsForPlan(planRunId).size(), 2);
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->lastRun.outcome, RunOutcome::Failed);
        QCOMPARE(f.store.find(QStringLiteral("TC-107"))->lastRun.outcome, RunOutcome::Passed);
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->shots.size(), 1);
        QVERIFY(!f.store.find(QStringLiteral("TC-103"))->shots.first().runId.isEmpty());   // sellada a su ejecución

        QSignalSpy released(&f.store, &TestCaseStore::filesReleased);
        QSignalSpy changed(&f.history, &RunHistoryStore::historyChanged);
        const int saves = f.historyRepo->saves;
        QVERIFY(f.history.removePlanRun(planRunId));

        QVERIFY(!f.history.findPlan(planRunId));
        QVERIFY(f.history.runsForPlan(planRunId).isEmpty());
        QCOMPARE(f.history.runs().size(), 1);   // la suelta sigue
        QCOMPARE(f.history.plans().size(), 0);
        QCOMPARE(f.historyRepo->saves, saves + 1);
        QCOMPARE(changed.count(), 1);
        // Las evidencias de esas ejecuciones se sueltan y sus ficheros se liberan.
        QVERIFY(f.store.find(QStringLiteral("TC-103"))->shots.isEmpty());
        QCOMPARE(released.count(), 1);
        QCOMPARE(released.first().first().toStringList(), QStringList{QStringLiteral("/tmp/qaflow-test/cap_001.png")});
        // La "última ejecución" vuelve a la anterior que queda, o a "sin ejecutar" si no queda ninguna.
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->lastRun.outcome, RunOutcome::Passed);
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->lastRun.at, looseAt);
        QCOMPARE(f.store.find(QStringLiteral("TC-107"))->lastRun.outcome, RunOutcome::None);

        QVERIFY(!f.history.removePlanRun(planRunId));   // ya no existe
    }

    void removingAPlanRunLeavesOtherCyclesAndTheirEvidenceAlone() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Ciclo 1"));
        const QString first = f.run.planRunId();
        f.store.addShot(QStringLiteral("TC-103"), Screenshot{1, 1, QStringLiteral("cap_001.png"), QStringLiteral("/tmp/qaflow-test/cap_001.png"), {}});
        f.run.mark(StepResult::Pass);
        f.run.finish();
        f.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Ciclo 2"));
        const QString second = f.run.planRunId();
        f.run.mark(StepResult::Block);
        f.run.finish();
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->lastRun.outcome, RunOutcome::Blocked);

        QSignalSpy released(&f.store, &TestCaseStore::filesReleased);
        QVERIFY(f.history.removePlanRun(second));
        QVERIFY(f.history.findPlan(first));
        QCOMPARE(f.history.runsForPlan(first).size(), 1);
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->shots.size(), 1);   // la del ciclo 1 sigue
        QCOMPARE(released.count(), 0);                                       // nada que borrar del disco
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->lastRun.outcome, RunOutcome::Passed);
        QCOMPARE(f.history.report(first).executed, 1);
    }

    // ---- Traer ejecuciones a un ciclo y quitar casos de él ---------------------------------

    // Una ejecución hecha fuera (suelta) se trae a un ciclo terminado sin volver a ejecutarla: el ciclo
    // gana el caso con su veredicto, sus evidencias y sus bugs, y la original no cambia.
    void importingARunBringsItsVerdictEvidenceAndBugsIntoTheCycle() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-101")}, QStringLiteral("Regresión"));
        const QString cycle = f.run.planRunId();
        while (!f.run.state().finished) f.run.mark(StepResult::Pass);
        f.run.finish();

        // Suelta: TC-103 falla, con una captura y un bug.
        f.run.start(QStringLiteral("TC-103"));
        f.store.addShot(QStringLiteral("TC-103"), Screenshot{50, 1, QStringLiteral("cap_050.png"), QStringLiteral("/tmp/qaflow-test/cap_050.png"), {}});
        IssueLink bug;
        bug.key = QStringLiteral("SHOP-50");
        bug.caseId = QStringLiteral("TC-103");
        bug.runId = f.run.state().runId;
        bug.step = 1;
        bug.createdAt = QDateTime::currentDateTime();
        f.bugLedger.recordIssue(bug);
        f.run.mark(StepResult::Fail);
        f.run.finish();
        const RunRecord loose = f.history.runsForCase(QStringLiteral("TC-103")).first();
        QVERIFY(loose.planRunId.isEmpty());
        const LastRun lastBefore = f.store.find(QStringLiteral("TC-103"))->lastRun;
        QVERIFY(f.history.report(cycle).bugs().isEmpty());

        const auto imported = f.history.importRun(cycle, loose.id);
        QVERIFY(imported);
        QVERIFY(imported->imported);
        QCOMPARE(imported->continuesRunId, loose.id);
        QCOMPARE(imported->planRunId, cycle);
        QVERIFY(imported->id != loose.id);
        QCOMPARE(static_cast<int>(imported->verdict), static_cast<int>(Verdict::Fallido));
        QVERIFY(std::all_of(imported->steps.cbegin(), imported->steps.cend(), [](const RunRecordStep& st) { return st.inherited; }));

        const PlanReport report = f.history.report(cycle);
        QVERIFY(report.plan.isFinished());   // no se reabre: no hay nada que ejecutar
        QCOMPARE(report.plan.caseIds, (QStringList{QStringLiteral("TC-101"), QStringLiteral("TC-103")}));
        QCOMPARE(report.executed, 2);
        QCOMPARE(report.rows[1].run.id, imported->id);
        QCOMPARE(f.history.evidenceOf(report.rows[1].run).size(), 1);   // la de la original, sin copiar ficheros
        QCOMPARE(f.history.evidenceOf(report.rows[1].run).first().runId, loose.id);
        QCOMPARE(report.rows[1].bugs.size(), 1);
        QCOMPARE(report.bugs().size(), 1);
        // La original sigue igual y el caso no cuenta una ejecución nueva.
        QVERIFY(f.history.findRun(loose.id)->planRunId.isEmpty());
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->lastRun.at, lastBefore.at);
        // Y se publica con su bug colgado de su paso.
        f.settings.updateTracker([](TrackerSettings& t) { t.zephyr = true; });
        f.publish.publish(report, [](const PublishResult&) {});
        QCOMPARE(f.zephyr->published.size(), 1);
        const QList<PublishDefect> defects = f.zephyr->published[0].cases[1].defects;
        QCOMPARE(f.zephyr->published[0].cases[1].attachments.size(), 0);   // el fichero de prueba no existe en disco
        QCOMPARE(defects.size(), 1);
        QCOMPARE(defects.first().key, QStringLiteral("SHOP-50"));
        QCOMPARE(defects.first().step, 1);

        // Dos veces la misma no; una del propio ciclo, tampoco.
        QVERIFY(!f.history.importRun(cycle, loose.id));
        QVERIFY(!f.history.importRun(cycle, report.rows[0].run.id));
        QVERIFY(!f.history.importRun(QStringLiteral("PR-9999"), loose.id));
    }

    // Traída sobre un caso que el ciclo ya tenía, pasa a ser la que cuenta (aunque se hiciera antes).
    void anImportedRunReplacesTheCaseResultInTheCycle() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-103"));
        f.run.mark(StepResult::Pass);
        f.run.finish();
        const QString older = f.history.runsForCase(QStringLiteral("TC-103")).first().id;
        f.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Regresión"));
        const QString cycle = f.run.planRunId();
        f.run.mark(StepResult::Fail);
        f.run.finish();
        QCOMPARE(f.history.report(cycle).failed, 1);
        QVERIFY(f.history.importRun(cycle, older));
        const PlanReport report = f.history.report(cycle);
        QCOMPARE(report.passed, 1);
        QCOMPARE(report.failed, 0);
        QCOMPARE(report.plan.caseIds.size(), 1);
    }

    // Quitar un caso de un ciclo: sale de la lista, sus ejecuciones allí se borran con lo que se capturó
    // en ellas y su «última ejecución» vuelve a la anterior. Lo que otro sitio usa no se quita.
    void removingACaseFromACycleDropsItsRunsThere() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-103"));   // antes, suelta: superada
        f.run.mark(StepResult::Pass);
        f.run.finish();
        const RunRecord before = f.history.runsForCase(QStringLiteral("TC-103")).first();
        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"));
        const QString cycle = f.run.planRunId();
        f.store.addShot(QStringLiteral("TC-103"), Screenshot{60, 1, QStringLiteral("cap_060.png"), QStringLiteral("/tmp/qaflow-test/cap_060.png"), {}});
        f.run.mark(StepResult::Fail);
        f.run.finish();
        f.run.mark(StepResult::Pass);
        f.run.finish();
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->lastRun.outcome, RunOutcome::Failed);

        // En curso no se toca, y el único caso tampoco.
        const QString other = f.history.startPlan(QStringLiteral("Otro"), {QStringLiteral("TC-101")});
        QVERIFY(!f.history.cannotRemoveCase(other, QStringLiteral("TC-101")).isEmpty());
        f.history.finishPlan(other);
        QVERIFY(!f.history.cannotRemoveCase(other, QStringLiteral("TC-101")).isEmpty());
        QVERIFY(!f.history.cannotRemoveCase(cycle, QStringLiteral("TC-101")).isEmpty());   // no es del ciclo

        // Traída a otro ciclo, la ejecución de aquí se usa allí: hay que quitarla de allí antes.
        const QString cycleRun = f.history.runsForPlan(cycle).first().id;
        const auto copy = f.history.importRun(other, cycleRun);
        QVERIFY(copy);
        QVERIFY(f.history.cannotRemoveCase(cycle, QStringLiteral("TC-103")).contains(copy->id));
        QVERIFY(!f.history.removeCaseFromCycle(cycle, QStringLiteral("TC-103")));
        // Quitar la traída no toca la evidencia de la original.
        QVERIFY(f.history.cannotRemoveCase(other, QStringLiteral("TC-103")).isEmpty());
        QVERIFY(f.history.removeCaseFromCycle(other, QStringLiteral("TC-103")));
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->shotsOfRun(cycleRun).size(), 1);
        QVERIFY(f.history.dependentsOf({cycleRun}).isEmpty());

        QVERIFY(f.history.removeCaseFromCycle(cycle, QStringLiteral("TC-103")));
        QCOMPARE(f.history.findPlan(cycle)->caseIds, QStringList{QStringLiteral("TC-107")});
        QVERIFY(!f.history.findRun(cycleRun));
        QVERIFY(f.store.find(QStringLiteral("TC-103"))->shotsOfRun(cycleRun).isEmpty());
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->lastRun.outcome, RunOutcome::Passed);   // la suelta de antes
        QCOMPARE(f.store.find(QStringLiteral("TC-103"))->lastRun.at, before.finishedAt);
        const PlanReport report = f.history.report(cycle);
        QCOMPARE(report.total(), 1);
        QCOMPARE(report.passed, 1);
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

    // Quitar una evidencia de una ejecución sólo la quita de ésa: la traída la desvincula y la original
    // la conserva; quitada de la original, la traída la sigue teniendo. Cuando ya no la tiene nadie, se
    // borra del caso. Lo desvinculado sobrevive a recargar el historial.
    void removingEvidenceFromOneRunLeavesItInTheOthers() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-101")}, QStringLiteral("Regresión"));
        const QString cycle = f.run.planRunId();
        while (!f.run.state().finished) f.run.mark(StepResult::Pass);
        f.run.finish();
        f.run.start(QStringLiteral("TC-103"));
        f.store.addShot(QStringLiteral("TC-103"), Screenshot{70, 1, QStringLiteral("cap_070.png"), QStringLiteral("/tmp/qaflow-test/cap_070.png"), {}});
        f.store.addShot(QStringLiteral("TC-103"), Screenshot{71, 1, QStringLiteral("cap_071.png"), QStringLiteral("/tmp/qaflow-test/cap_071.png"), {}});
        f.run.mark(StepResult::Pass);
        f.run.finish();
        const QString original = f.history.runsForCase(QStringLiteral("TC-103")).first().id;
        const auto imported = f.history.importRun(cycle, original);
        QVERIFY(imported);
        auto ids = [&](const QString& runId) {
            QList<int> out;
            for (const auto& s : f.history.evidenceOf(*f.history.findRun(runId))) out << s.id;
            return out;
        };
        auto inCase = [&](int id) {
            const auto& shots = f.store.find(QStringLiteral("TC-103"))->shots;
            return std::any_of(shots.cbegin(), shots.cend(), [id](const Screenshot& s) { return s.id == id; });
        };
        QCOMPARE(ids(imported->id), (QList<int>{70, 71}));

        // De la traída: sólo se desvincula.
        QVERIFY(f.history.removeEvidence(imported->id, 70));
        QCOMPARE(ids(imported->id), QList<int>{71});
        QCOMPARE(ids(original), (QList<int>{70, 71}));
        QVERIFY(inCase(70));

        // De la original: la traída la conserva.
        QVERIFY(f.history.removeEvidence(original, 71));
        QCOMPARE(ids(original), QList<int>{70});
        QCOMPARE(ids(imported->id), QList<int>{71});
        QVERIFY(inCase(71));
        QVERIFY(!f.history.removeEvidence(original, 71));   // ya no la tiene

        // Se recarga igual.
        RunHistoryStore again(f.historyRepo, f.store);
        again.load();
        QCOMPARE(again.evidenceOf(*again.findRun(imported->id)).size(), 1);
        QCOMPARE(again.evidenceOf(*again.findRun(imported->id)).first().id, 71);

        // Ya no la tiene nadie más: se borra del caso.
        QVERIFY(f.history.removeEvidence(imported->id, 71));
        QVERIFY(ids(imported->id).isEmpty());
        QVERIFY(!inCase(71));
        QVERIFY(f.history.removeEvidence(original, 70));
        QVERIFY(!inCase(70));
    }

    // Borrar el ciclo de la original no se lleva la evidencia que la traída conserva.
    void deletingACycleKeepsTheEvidenceOtherRunsStillHave() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Uno"));
        const QString first = f.run.planRunId();
        f.store.addShot(QStringLiteral("TC-103"), Screenshot{80, 1, QStringLiteral("cap_080.png"), QStringLiteral("/tmp/qaflow-test/cap_080.png"), {}});
        while (!f.run.state().finished) f.run.mark(StepResult::Pass);
        f.run.finish();
        const QString original = f.history.runsForPlan(first).first().id;
        f.run.startSequence({QStringLiteral("TC-101")}, QStringLiteral("Dos"));
        const QString second = f.run.planRunId();
        while (!f.run.state().finished) f.run.mark(StepResult::Pass);
        f.run.finish();
        const auto imported = f.history.importRun(second, original);
        QVERIFY(imported);
        QVERIFY(f.history.removeEvidence(original, 80));   // la traída la conserva
        QVERIFY(f.history.removePlanRun(first));
        const auto& shots = f.store.find(QStringLiteral("TC-103"))->shots;
        QVERIFY(std::any_of(shots.cbegin(), shots.cend(), [](const Screenshot& s) { return s.id == 80; }));
        QCOMPARE(f.history.evidenceOf(*f.history.findRun(imported->id)).size(), 1);
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
