// RunController (application/RunController.h): ejecución paso a paso, cola del plan,
// correcciones y sesión persistente.

#include "support/AppFixture.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;

class RunControllerTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Continuar un ciclo -------------------------------------------------------------

    // Continuar repite sólo lo que se rompió, y cada caso se retoma en su paso: lo anterior se
    // conserva (marcado como heredado) y no hay que volver a probarlo.
    void continuingACycleRepeatsOnlyTheBrokenCasesFromTheirBrokenStep() {
        AppFixture f;
        // Un ciclo con tres casos: uno pasa, uno falla en su segundo paso y otro queda bloqueado.
        f.run.startSequence({QStringLiteral("TC-101"), QStringLiteral("TC-102"), QStringLiteral("TC-103")},
                            QStringLiteral("Regresión"), QStringLiteral("PL-0001"), QStringLiteral("QA"));
        const QString first = f.run.planRunId();
        while (!f.run.state().finished) f.run.mark(StepResult::Pass);   // TC-101 (3 pasos)
        f.run.finish();
        f.run.mark(StepResult::Pass);                                   // TC-102, paso 1
        f.run.setNote(QStringLiteral("el cupón no descuenta"));
        f.run.mark(StepResult::Fail);                                   // TC-102, paso 2
        f.run.finish();
        f.run.mark(StepResult::Block);                                  // TC-103 (1 paso)
        f.run.finish();
        QVERIFY(f.history.findPlan(first)->isFinished());
        QCOMPARE(f.history.report(first).brokenCaseIds(), (QStringList{QStringLiteral("TC-102"), QStringLiteral("TC-103")}));

        QVERIFY(f.run.continueCycle(first, QStringLiteral("Staging")));
        const PlanRun* second = f.history.findPlan(f.run.planRunId());
        QVERIFY(second);
        QCOMPARE(second->continuesCycleId, first);
        QCOMPARE(second->caseIds, (QStringList{QStringLiteral("TC-102"), QStringLiteral("TC-103")}));
        QCOMPARE(second->environment, QStringLiteral("Staging"));

        // TC-102 se retoma en el paso 2: el 1 viene de la ejecución anterior y ya está marcado.
        QCOMPARE(f.run.state().caseId, QStringLiteral("TC-102"));
        QCOMPARE(f.run.state().idx, 1);
        QVERIFY(f.run.state().isMarked(0));
        QVERIFY(f.run.state().results[0].inherited);
        QCOMPARE(static_cast<int>(f.run.state().results[0].result), static_cast<int>(StepResult::Pass));
        QVERIFY(!f.run.state().isMarked(1));
        QVERIFY(!f.run.continuesRunId().isEmpty());

        // Y al pasar ahora el paso que falló, el caso queda superado con los dos pasos archivados.
        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.state().finished);
        f.run.finish();
        const RunRecord& resumed = f.history.runs().last();
        QCOMPARE(resumed.caseId, QStringLiteral("TC-102"));
        QCOMPARE(static_cast<int>(resumed.verdict), static_cast<int>(Verdict::Superado));
        QCOMPARE(resumed.steps.size(), 2);
        QVERIFY(!resumed.continuesRunId.isEmpty());
        QCOMPARE(f.run.state().caseId, QStringLiteral("TC-103"));   // sigue con el otro caso roto
        QCOMPARE(f.run.state().idx, 0);                             // su único paso se rompió: se repite entero
    }

    void aCycleWithoutBrokenCasesIsNotContinued() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Regresión"), QStringLiteral("PL-0001"));
        const QString cycle = f.run.planRunId();
        f.run.mark(StepResult::Pass);
        f.run.finish();
        QVERIFY(!f.run.continueCycle(cycle));             // todo superado: no hay nada que continuar
        QVERIFY(!f.run.continueCycle(QStringLiteral("PR-9999")));   // y un ciclo que no existe, tampoco
    }

    // Si el caso cambió desde aquella ejecución, lo de antes no se da por bueno: se prueba entero.
    void anEditedCaseIsNotResumedFromTheOldResults() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-102")}, QStringLiteral("Regresión"), QStringLiteral("PL-0001"));
        const QString cycle = f.run.planRunId();
        f.run.mark(StepResult::Pass);
        f.run.mark(StepResult::Fail);
        f.run.finish();
        f.store.updateCase(QStringLiteral("TC-102"), [](TestCase& c) { c.steps[0].action = QStringLiteral("Otra cosa"); });

        QVERIFY(f.run.continueCycle(cycle));
        QCOMPARE(f.run.state().idx, 0);
        QVERIFY(!f.run.state().isMarked(0));
    }

    // ---- Flujo básico ------------------------------------------------------------------

    void marksStepsAndRecordsOutcomeOnFinish() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-102"));   // 2 pasos
        QVERIFY(f.run.isRunning());
        QCOMPARE(f.run.state().results.size(), 2);   // un registro por paso, pendientes de marcar
        f.run.mark(StepResult::Pass);
        QCOMPARE(f.run.state().idx, 1);
        f.run.setNote(QStringLiteral("se rompió"));
        f.run.mark(StepResult::Fail);
        QVERIFY(f.run.state().finished);
        QCOMPARE(f.run.state().results[1].note, QStringLiteral("se rompió"));
        QVERIFY(!f.run.finish());
        QCOMPARE(static_cast<int>(f.store.find(QStringLiteral("TC-102"))->lastRun.outcome), static_cast<int>(RunOutcome::Failed));
    }

    /// Un bloqueo ya no corta la ejecución: se sigue pudiendo recorrer (y reportar) el resto.
    void blockKeepsTheRunOpenAndIsRecordedOnCase() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-104"));   // 4 pasos
        f.run.mark(StepResult::Block);
        QVERIFY(!f.run.state().finished);
        QVERIFY(f.run.isRunning());
        QCOMPARE(f.run.state().idx, 1);   // avanza como cualquier otro veredicto
        QCOMPARE(static_cast<int>(f.run.state().verdict()), static_cast<int>(Verdict::Bloqueado));

        f.run.finish();   // cerrarla a medias la archiva con lo marcado
        QCOMPARE(static_cast<int>(f.store.find(QStringLiteral("TC-104"))->lastRun.outcome), static_cast<int>(RunOutcome::Blocked));
        QCOMPARE(f.history.runs().first().steps.size(), 1);
        QCOMPARE(f.history.runs().first().plannedSteps, 4);
    }

    /// Los pasos se recorren en cualquier orden; los huecos se archivan como N/A.
    void stepsCanBeVisitedInAnyOrderAndPendingOnesAreArchivedAsSkipped() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-104"));   // 4 pasos
        f.run.goTo(2);
        QCOMPARE(f.run.state().idx, 2);
        f.run.mark(StepResult::Fail);
        QCOMPARE(f.run.state().idx, 3);          // sigue por el siguiente pendiente
        QVERIFY(!f.run.state().finished);        // fallar tampoco corta la ejecución
        f.run.mark(StepResult::Pass);
        QCOMPARE(f.run.state().idx, 0);          // no queda nada por delante: vuelve al primer hueco
        QVERIFY(!f.run.state().finished);

        f.run.finish();
        const RunRecord& rec = f.history.runs().first();
        QCOMPARE(rec.steps.size(), 4);
        QCOMPARE(static_cast<int>(rec.steps[0].result), static_cast<int>(StepResult::Skip));   // nunca se marcó
        QCOMPARE(static_cast<int>(rec.steps[2].result), static_cast<int>(StepResult::Fail));
        QCOMPARE(static_cast<int>(rec.verdict), static_cast<int>(Verdict::Fallido));
    }

    /// Volver a un paso ya marcado permite cambiarlo sin perder el resto.
    void aMarkedStepCanBeVisitedAgainAndRemarked() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-102"));   // 2 pasos
        f.run.mark(StepResult::Fail);
        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.state().finished);

        f.run.goTo(0);                           // reabre la ejecución terminada
        QVERIFY(!f.run.state().finished);
        QVERIFY(f.run.isRunning());
        QCOMPARE(f.run.state().markedCount(), 2);
        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.state().finished);
        QCOMPARE(static_cast<int>(f.run.state().verdict()), static_cast<int>(Verdict::Superado));
    }

    void skipDoesNotAffectVerdict() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-102"));
        f.run.mark(StepResult::Skip);
        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.state().finished);
        QCOMPARE(static_cast<int>(f.run.state().verdict()), static_cast<int>(Verdict::Superado));
        f.run.finish();
        QCOMPARE(f.history.runs().first().count(StepResult::Skip), 1);
    }

    // ---- Correcciones ------------------------------------------------------------------

    void backReturnsToThePreviousStepWithItsNote() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-104"));
        f.run.mark(StepResult::Pass);
        f.run.setNote(QStringLiteral("dudoso"));
        f.run.mark(StepResult::Fail);
        QCOMPARE(f.run.state().idx, 2);

        f.run.back();
        QCOMPARE(f.run.state().idx, 1);
        QCOMPARE(f.run.state().note, QStringLiteral("dudoso"));
        QVERIFY(f.run.state().isMarked(1));     // el veredicto sigue puesto: volver no lo deshace
        QVERIFY(f.run.isRunning());
        f.run.next();
        QCOMPARE(f.run.state().idx, 2);
        QVERIFY(f.run.state().note.isEmpty());   // cada paso trae la suya
    }

    void backReopensAFinishedRun() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-103"));   // 1 paso
        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.state().finished);
        f.run.back();
        QVERIFY(!f.run.state().finished);
        QCOMPARE(f.run.state().idx, 0);
        QCOMPARE(f.history.runs().size(), 0);   // nada archivado todavía
    }

    void correctingAVerdictFromTheListDoesNotMoveTheRun() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-104"));
        f.run.mark(StepResult::Pass);
        f.run.mark(StepResult::Block);
        QCOMPARE(f.run.state().idx, 2);

        f.run.setResult(1, StepResult::Pass);
        QCOMPARE(f.run.state().idx, 2);
        QCOMPARE(static_cast<int>(f.run.state().verdict()), static_cast<int>(Verdict::Superado));
        f.run.setResult(0, StepResult::Fail);
        QCOMPARE(static_cast<int>(f.run.state().verdict()), static_cast<int>(Verdict::Fallido));
        f.run.setResult(9, StepResult::Pass);   // índice inválido: se ignora
        QCOMPARE(f.run.state().markedCount(), 2);
    }

    // ---- Archivado en el historial -----------------------------------------------------

    void finishedRunIsArchivedAsSnapshot() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-102"));
        f.run.mark(StepResult::Pass);
        f.run.setNote(QStringLiteral("se rompió"));
        f.run.mark(StepResult::Fail);
        QCOMPARE(f.history.runs().size(), 0);   // aún no se ha cerrado
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
        QVERIFY(!r.steps[0].action.isEmpty());   // instantánea del texto del paso
        QVERIFY(f.historyRepo->saves > 0);
    }

    // El id se reserva al arrancar, no al archivar: los bugs que se reportan mientras corre la
    // ejecución se enlazan con ella, y el registro que se guarda es el que ellos nombran.
    void theRunIsNamedFromTheStartAndKeepsThatNameWhenArchived() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-102"));
        const QString reserved = f.run.state().runId;
        QCOMPARE(reserved, QStringLiteral("R-0001"));
        f.run.mark(StepResult::Pass);
        f.run.mark(StepResult::Fail);
        f.run.finish();
        QCOMPARE(f.history.runs().first().id, reserved);

        // Y la siguiente estrena el suyo, también antes de terminar.
        f.run.start(QStringLiteral("TC-103"));
        QCOMPARE(f.run.state().runId, QStringLiteral("R-0002"));
        f.run.mark(StepResult::Pass);
        f.run.finish();
        QCOMPARE(f.history.runs().last().id, QStringLiteral("R-0002"));

        // La sesión guardada lo lleva: al restaurarla, la ejecución sigue llamándose igual.
        f.run.start(QStringLiteral("TC-107"));
        const QString restored = f.run.state().runId;
        f.run.persistSessionNow();
        RunController again(f.store, f.history, f.sessionRepo);
        again.load();
        QCOMPARE(again.state().runId, restored);
    }

    void startingAnotherCaseArchivesTheFinishedOne() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-103"));
        f.run.mark(StepResult::Pass);
        f.run.start(QStringLiteral("TC-107"));   // sin pulsar "Finalizar"
        QCOMPARE(f.history.runs().size(), 1);
        QCOMPARE(static_cast<int>(f.store.find(QStringLiteral("TC-103"))->lastRun.outcome), static_cast<int>(RunOutcome::Passed));
    }

    void restartArchivesEachAttempt() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-103"));
        f.run.mark(StepResult::Fail);
        f.run.restart();
        f.run.mark(StepResult::Pass);
        f.run.finish();
        const auto runs = f.history.runsForCase(QStringLiteral("TC-103"));
        QCOMPARE(runs.size(), 2);
        QCOMPARE(static_cast<int>(runs[0].verdict), static_cast<int>(Verdict::Superado));   // la más reciente primero
        QCOMPARE(static_cast<int>(runs[1].verdict), static_cast<int>(Verdict::Fallido));
    }

    // ---- Cola del plan -----------------------------------------------------------------

    void sequenceAdvancesThroughQueueAndSelectsEachCase() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")});   // 1 paso cada uno
        QCOMPARE(f.run.state().caseId, QStringLiteral("TC-103"));
        QCOMPARE(f.run.queuedCount(), 1);
        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.finish());
        QCOMPARE(f.run.state().caseId, QStringLiteral("TC-107"));
        QCOMPARE(f.store.selectedId(), QStringLiteral("TC-107"));
        f.run.mark(StepResult::Pass);
        QVERIFY(!f.run.finish());
        QVERIFY(!f.run.isRunning());
    }

    void sequenceOpensAndClosesAPlanRun() {
        AppFixture f;
        QSignalSpy completed(&f.run, &RunController::planCompleted);
        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"), QStringLiteral("PL-0001"));
        const QString planRunId = f.run.planRunId();
        QCOMPARE(planRunId, QStringLiteral("PR-0001"));
        QCOMPARE(f.history.findPlan(planRunId)->planId, QStringLiteral("PL-0001"));
        QVERIFY(!f.history.findPlan(planRunId)->isFinished());

        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.finish());
        QCOMPARE(completed.count(), 0);
        f.run.mark(StepResult::Fail);
        QVERIFY(!f.run.finish());
        QCOMPARE(completed.count(), 1);
        QVERIFY(f.run.planRunId().isEmpty());
        QVERIFY(f.history.findPlan(planRunId)->isFinished());
    }

    void abandonClosesThePlanRun() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Parcial"));
        const QString planRunId = f.run.planRunId();
        f.run.mark(StepResult::Pass);
        f.run.finish();
        f.run.abandon();
        QVERIFY(!f.run.isRunning());
        QVERIFY(f.run.planRunId().isEmpty());
        QVERIFY(f.history.findPlan(planRunId)->isFinished());
        QCOMPARE(f.history.runsForPlan(planRunId).size(), 1);
    }

    // ---- Ir de un caso a otro del ciclo -------------------------------------------------

    // Ir a otro caso deja el actual en pausa, sin archivarlo, y al volver se retoma donde se dejó.
    void goingToAnotherCaseParksTheCurrentOneAndResumesItLater() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-104"), QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"));
        QCOMPARE(f.run.planCases(), (QStringList{QStringLiteral("TC-104"), QStringLiteral("TC-103"), QStringLiteral("TC-107")}));
        const QString runId = f.run.state().runId;
        f.run.mark(StepResult::Pass);
        f.run.setNote(QStringLiteral("a medias"));

        QVERIFY(f.run.goToCase(QStringLiteral("TC-107")));
        QCOMPARE(f.run.state().caseId, QStringLiteral("TC-107"));
        QCOMPARE(f.store.selectedId(), QStringLiteral("TC-107"));
        QVERIFY(f.run.isQueued(QStringLiteral("TC-104")));
        QVERIFY(!f.run.isQueued(QStringLiteral("TC-107")));
        QCOMPARE(f.run.queuedCount(), 2);
        const RunState* parked = f.run.parkedRun(QStringLiteral("TC-104"));
        QVERIFY(parked);
        QCOMPARE(parked->markedCount(), 1);
        QVERIFY(f.history.runsForPlan(f.run.planRunId()).isEmpty());   // nada archivado todavía

        // «Siguiente caso» sigue el orden del plan: vuelve a TC-104, donde se dejó.
        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.finish());
        QCOMPARE(f.run.state().caseId, QStringLiteral("TC-104"));
        QCOMPARE(f.run.state().runId, runId);
        QCOMPARE(f.run.state().idx, 1);
        QCOMPARE(f.run.state().markedCount(), 1);
        QCOMPARE(f.run.state().note, QStringLiteral("a medias"));   // la nota del paso en pantalla, también
        QVERIFY(!f.run.parkedRun(QStringLiteral("TC-104")));
        QCOMPARE(f.history.runsForPlan(f.run.planRunId()).size(), 1);
    }

    void onlyQueuedCasesOfTheCycleCanBeReached() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-103"));
        QVERIFY(!f.run.goToCase(QStringLiteral("TC-107")));   // un caso suelto no tiene ciclo
        QVERIFY(f.run.planCases().isEmpty());

        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107"), QStringLiteral("TC-104")}, QStringLiteral("Regresión"));
        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.finish());                                   // TC-103 archivado
        QVERIFY(!f.run.goToCase(QStringLiteral("TC-103")));
        QVERIFY(!f.run.goToCase(QStringLiteral("TC-101")));   // no es del plan
        QVERIFY(!f.run.goToCase(QStringLiteral("TC-107")));   // ya está en pantalla
        QCOMPARE(f.run.state().caseId, QStringLiteral("TC-107"));
    }

    // Al cerrar el ciclo, lo aparcado con todos sus pasos marcados se archiva; lo que quedó a medias, no.
    void closingTheCycleArchivesTheFinishedParkedCases() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-104"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"));
        const QString cycle = f.run.planRunId();
        f.run.mark(StepResult::Fail);                              // TC-103 terminado, sin cerrar
        QVERIFY(f.run.goToCase(QStringLiteral("TC-104")));
        f.run.mark(StepResult::Pass);                              // TC-104 a medias
        QVERIFY(f.run.goToCase(QStringLiteral("TC-107")));
        f.run.abandon();
        const auto runs = f.history.runsForPlan(cycle);
        QCOMPARE(runs.size(), 1);
        QCOMPARE(runs[0].caseId, QStringLiteral("TC-103"));
        QCOMPARE(static_cast<int>(runs[0].verdict), static_cast<int>(Verdict::Fallido));
        QVERIFY(!f.run.parkedRun(QStringLiteral("TC-104")));
    }

    // ---- Pausa -----------------------------------------------------------------------------

    // En pausa el reloj no corre y no se marca ni se cambia de paso; al reanudar, todo sigue igual.
    void pausingFreezesTheClockAndTheSteps() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-101"));   // 3 pasos
        f.run.pause();
        QVERIFY(f.run.isPaused());
        QVERIFY(f.run.isRunning());
        QVERIFY(!f.run.state().stepStartedAt.isValid());
        const QDateTime later = QDateTime::currentDateTime().addSecs(3600);
        QCOMPARE(f.run.state().currentStepSecs(later), f.run.state().stepElapsedSecs);   // la hora en pausa no cuenta

        f.run.mark(StepResult::Pass);
        f.run.next();
        f.run.setResult(1, StepResult::Fail);
        QCOMPARE(f.run.state().idx, 0);
        QCOMPARE(f.run.state().markedCount(), 0);
        QVERIFY(!f.run.canGoNext());

        f.run.resume();
        QVERIFY(!f.run.isPaused());
        QVERIFY(f.run.state().stepStartedAt.isValid());
        f.run.mark(StepResult::Pass);
        QCOMPARE(f.run.state().idx, 1);
        QCOMPARE(f.run.state().markedCount(), 1);
    }

    void aFinishedRunCannotBePaused() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-103"));   // 1 paso
        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.state().finished);
        f.run.togglePause();
        QVERIFY(!f.run.isPaused());
        QVERIFY(!f.run.state().paused);
    }

    void aPausedRunStaysPausedAfterRestart() {
        AppFixture f;
        f.run.start(QStringLiteral("TC-104"));
        f.run.mark(StepResult::Pass);
        f.run.pause();
        f.run.persistSessionNow();

        RunController again(f.store, f.history, f.sessionRepo);
        again.load();
        QVERIFY(again.isPaused());
        QVERIFY(!again.state().stepStartedAt.isValid());
        QCOMPARE(again.state().idx, 1);
        again.resume();
        QVERIFY(!again.isPaused());
        QVERIFY(again.state().stepStartedAt.isValid());
    }

    // Ir a otro caso del ciclo es ponerse a probar: el nuevo no hereda la pausa, y el aparcado se retoma sin ella.
    void goingToAnotherCaseLeavesThePause() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-104"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"));
        f.run.pause();
        QVERIFY(f.run.goToCase(QStringLiteral("TC-107")));
        QVERIFY(!f.run.isPaused());
        QVERIFY(f.run.goToCase(QStringLiteral("TC-104")));
        QVERIFY(!f.run.isPaused());
        QVERIFY(f.run.state().stepStartedAt.isValid());
    }

    // ---- Sesión persistente ------------------------------------------------------------

    void parkedCasesSurviveRestart() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-104"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"));
        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.goToCase(QStringLiteral("TC-107")));
        f.run.persistSessionNow();

        RunController again(f.store, f.history, f.sessionRepo);
        again.load();
        QCOMPARE(again.state().caseId, QStringLiteral("TC-107"));
        const RunState* parked = again.parkedRun(QStringLiteral("TC-104"));
        QVERIFY(parked);
        QCOMPARE(parked->markedCount(), 1);
        QVERIFY(again.goToCase(QStringLiteral("TC-104")));
        QCOMPARE(again.state().idx, 1);
        QCOMPARE(again.state().markedCount(), 1);
    }

    void sessionSurvivesRestart() {
        AppFixture f;
        f.run.startSequence({QStringLiteral("TC-104"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"));
        f.run.mark(StepResult::Pass);
        f.run.setNote(QStringLiteral("a medias"));
        f.run.persistSessionNow();
        QVERIFY(f.sessionRepo->session.has_value());

        // "Nueva sesión": otro controlador sobre los mismos repositorios.
        RunController again(f.store, f.history, f.sessionRepo);
        again.load();
        QVERIFY(again.isRunning());
        QCOMPARE(again.state().caseId, QStringLiteral("TC-104"));
        QCOMPARE(again.state().idx, 1);
        QCOMPARE(again.state().markedCount(), 1);
        QCOMPARE(again.state().note, QStringLiteral("a medias"));
        QCOMPARE(again.queuedCount(), 1);
        QCOMPARE(again.planRunId(), f.run.planRunId());
        QCOMPARE(f.store.selectedId(), QStringLiteral("TC-104"));

        again.mark(StepResult::Pass); again.mark(StepResult::Pass); again.mark(StepResult::Pass);
        QVERIFY(again.finish());   // sigue con TC-107 del plan restaurado
        again.mark(StepResult::Pass);
        QVERIFY(!again.finish());
        QVERIFY(!f.sessionRepo->session.has_value());   // sin ejecución → sesión borrada
        QCOMPARE(f.history.report(QStringLiteral("PR-0001")).executed, 2);
    }

    void sessionForMissingCaseIsDiscarded() {
        AppFixture f;
        RunSession s;
        s.run.caseId = QStringLiteral("TC-999");
        f.sessionRepo->session = s;
        f.run.load();
        QVERIFY(!f.run.isRunning());
        QVERIFY(!f.sessionRepo->session.has_value());
    }

    void sessionTruncatesResultsIfCaseLostSteps() {
        AppFixture f;
        RunSession s;
        s.run.caseId = QStringLiteral("TC-103");   // 1 paso
        s.run.results = {StepRecord{StepResult::Pass, {}, 0, true}, StepRecord{StepResult::Pass, {}, 0, true}};
        f.sessionRepo->session = s;
        f.run.load();
        QCOMPARE(f.run.state().results.size(), 1);
        QVERIFY(f.run.state().finished);
    }
};

QTEST_APPLESS_MAIN(RunControllerTest)
#include "test_run_controller.moc"
