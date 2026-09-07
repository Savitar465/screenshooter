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
    std::optional<PlanCollection> plans;
    int saves = 0;
    std::optional<QList<TestCase>> loadCases() override { return cases; }
    bool saveCases(const QList<TestCase>& c) override { cases = c; ++saves; return true; }
    std::optional<PlanCollection> loadPlans() override { return plans; }
    bool savePlans(const PlanCollection& p) override { plans = p; return true; }
};

class MemoryHistoryRepo : public IRunHistoryRepository {
public:
    std::optional<RunHistory> history;
    int saves = 0;
    std::optional<RunHistory> loadHistory() override { return history; }
    bool saveHistory(const RunHistory& h) override { history = h; ++saves; return true; }
};

class MemorySessionRepo : public IRunSessionRepository {
public:
    std::optional<RunSession> session;
    std::optional<RunSession> loadSession() override { return session; }
    bool saveSession(const RunSession& s) override { session = s; return true; }
    void clearSession() override { session.reset(); }
};

/// Todo lo necesario para ejecutar casos en memoria.
struct Fixture {
    std::shared_ptr<MemoryRepo> repo = std::make_shared<MemoryRepo>();
    std::shared_ptr<MemoryHistoryRepo> historyRepo = std::make_shared<MemoryHistoryRepo>();
    std::shared_ptr<MemorySessionRepo> sessionRepo = std::make_shared<MemorySessionRepo>();
    TestCaseStore store{repo};
    RunHistoryStore history{historyRepo, store};
    RunController run{store, history, sessionRepo};
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

    void skipDoesNotAffectVerdict() {
        Fixture f;
        f.run.start(QStringLiteral("TC-102")); // 2 pasos
        f.run.mark(StepResult::Skip);
        f.run.mark(StepResult::Pass);
        QVERIFY(f.run.state().finished);
        QCOMPARE(static_cast<int>(f.run.state().verdict()), static_cast<int>(Verdict::Superado));
        QCOMPARE(f.run.state().count(StepResult::Skip), 1);
        f.run.finish();
        QCOMPARE(f.history.runs().first().count(StepResult::Skip), 1);
    }

    void backReopensPreviousStep() {
        Fixture f;
        f.run.start(QStringLiteral("TC-104")); // 4 pasos
        f.run.mark(StepResult::Pass);
        f.run.setNote(QStringLiteral("dudoso"));
        f.run.mark(StepResult::Fail);
        QCOMPARE(f.run.state().idx, 2);
        f.run.back();
        QCOMPARE(f.run.state().idx, 1);
        QCOMPARE(f.run.state().results.size(), 1);
        QCOMPARE(f.run.state().note, QStringLiteral("dudoso")); // la nota vuelve al campo
        QVERIFY(f.run.isRunning());
        // También reabre una ejecución terminada.
        f.run.mark(StepResult::Pass); f.run.mark(StepResult::Pass); f.run.mark(StepResult::Pass);
        QVERIFY(f.run.state().finished);
        f.run.back();
        QVERIFY(!f.run.state().finished);
        QCOMPARE(f.run.state().idx, 3);
        QCOMPARE(f.history.runs().size(), 0); // nada archivado todavía
    }

    void correctingAVerdictUnblocksTheRun() {
        Fixture f;
        f.run.start(QStringLiteral("TC-104"));
        f.run.mark(StepResult::Pass);
        f.run.mark(StepResult::Block);
        QVERIFY(f.run.state().finished);
        f.run.setResult(1, StepResult::Pass);
        QVERIFY(!f.run.state().finished);
        QCOMPARE(f.run.state().idx, 2);
        f.run.setResult(0, StepResult::Fail);
        QCOMPARE(static_cast<int>(f.run.state().verdict()), static_cast<int>(Verdict::Fallido));
        f.run.setResult(9, StepResult::Pass); // índice inválido: se ignora
        QCOMPARE(f.run.state().results.size(), 2);
    }

    void sessionSurvivesRestart() {
        Fixture f;
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
        QCOMPARE(again.state().results.size(), 1);
        QCOMPARE(again.state().note, QStringLiteral("a medias"));
        QCOMPARE(again.queuedCount(), 1);
        QCOMPARE(again.planRunId(), f.run.planRunId());
        QCOMPARE(f.store.selectedId(), QStringLiteral("TC-104"));

        again.mark(StepResult::Pass); again.mark(StepResult::Pass); again.mark(StepResult::Pass);
        QVERIFY(again.finish());          // sigue con TC-107 del plan restaurado
        again.mark(StepResult::Pass);
        QVERIFY(!again.finish());
        QVERIFY(!f.sessionRepo->session.has_value()); // sin ejecución → sesión borrada
        QCOMPARE(f.history.report(f.run.planRunId().isEmpty() ? QStringLiteral("PR-0001") : f.run.planRunId()).executed, 2);
    }

    void sessionForMissingCaseIsDiscarded() {
        Fixture f;
        RunSession s;
        s.run.caseId = QStringLiteral("TC-999");
        f.sessionRepo->session = s;
        f.run.load();
        QVERIFY(!f.run.isRunning());
        QVERIFY(!f.sessionRepo->session.has_value());
    }

    void estimateUsesRealDurations() {
        Fixture f;
        PlanStore plan(f.repo, f.store, f.history);
        plan.load();
        QCOMPARE(plan.estimatedTime(), QStringLiteral("33 min")); // 11 pasos × 3 min, sin historial
        QVERIFY(plan.estimateBasis().contains(QStringLiteral("sin historial")));

        RunRecord r;   // TC-104 (4 pasos) tardó 2 min → 30 s/paso
        r.caseId = QStringLiteral("TC-104");
        r.durationSecs = 120;
        for (int i = 0; i < 4; ++i) r.steps.append(RunRecordStep{{}, {}, StepResult::Pass, {}, 30});
        f.history.addRun(r);
        // TC-104 por su propia media (120 s); los otros 7 pasos por la media global (30 s) = 210 s.
        QCOMPARE(plan.estimatedSecs(), 330);
        QCOMPARE(plan.estimatedTime(), QStringLiteral("6 min"));
        QCOMPARE(plan.estimateBasis(), QStringLiteral("según 1 ejecución"));
    }

    void removeCaseSelectsNeighbourAndCanBeUndone() {
        Fixture f;
        QSignalSpy undoSpy(&f.store, &TestCaseStore::undoAvailable);
        QSignalSpy filesSpy(&f.store, &TestCaseStore::filesReleased);
        f.store.addShot(QStringLiteral("TC-104"), Screenshot{1, 1, QStringLiteral("a.png"), QStringLiteral("/tmp/a.png")});
        f.store.select(QStringLiteral("TC-104"));
        f.store.removeCase(QStringLiteral("TC-104"));
        QCOMPARE(f.store.cases().size(), 6);
        QVERIFY(!f.store.find(QStringLiteral("TC-104")));
        QCOMPARE(f.store.selectedId(), QStringLiteral("TC-105")); // el siguiente en la lista
        QCOMPARE(undoSpy.count(), 1);
        QVERIFY(f.store.canUndo());
        QCOMPARE(filesSpy.count(), 0);            // el fichero espera al deshacer
        QVERIFY(f.store.undo());
        QCOMPARE(f.store.cases().size(), 7);
        QCOMPARE(f.store.selectedId(), QStringLiteral("TC-104"));
        QCOMPARE(f.store.find(QStringLiteral("TC-104"))->shots.size(), 1);
        QVERIFY(!f.store.canUndo());
        QCOMPARE(filesSpy.count(), 0);
    }

    void committingUndoReleasesFiles() {
        Fixture f;
        QSignalSpy filesSpy(&f.store, &TestCaseStore::filesReleased);
        f.store.addShot(QStringLiteral("TC-104"), Screenshot{1, 1, QStringLiteral("a.png"), QStringLiteral("/tmp/a.png")});
        f.store.removeShot(QStringLiteral("TC-104"), 1);
        QVERIFY(f.store.canUndo());
        // Cualquier otra edición invalida el deshacer y libera el fichero.
        f.store.updateCase(QStringLiteral("TC-101"), [](TestCase& c) { c.title = QStringLiteral("x"); });
        QVERIFY(!f.store.canUndo());
        QCOMPARE(filesSpy.count(), 1);
        QCOMPARE(filesSpy.first().first().toStringList(), QStringList{QStringLiteral("/tmp/a.png")});
    }

    void removeStepIsUndoable() {
        Fixture f;
        f.store.removeStep(QStringLiteral("TC-104"), 0);
        QCOMPARE(f.store.find(QStringLiteral("TC-104"))->steps.size(), 3);
        QVERIFY(f.store.undo());
        QCOMPARE(f.store.find(QStringLiteral("TC-104"))->steps.size(), 4);
    }

    void duplicateCaseCopiesContentNotResults() {
        Fixture f;
        f.store.addShot(QStringLiteral("TC-104"), Screenshot{1, 1, QStringLiteral("a.png"), {}});
        const QString id = f.store.duplicateCase(QStringLiteral("TC-104"));
        QCOMPARE(id, QStringLiteral("TC-108"));
        const TestCase* c = f.store.find(id);
        QVERIFY(c);
        QVERIFY(c->title.endsWith(QStringLiteral("(copia)")));
        QCOMPARE(c->steps.size(), 4);
        QCOMPARE(c->suite, QStringLiteral("Checkout"));
        QCOMPARE(static_cast<int>(c->status), static_cast<int>(CaseStatus::Borrador));
        QCOMPARE(static_cast<int>(c->lastRun.outcome), static_cast<int>(RunOutcome::None));
        QVERIFY(c->shots.isEmpty());
        QCOMPARE(f.store.cases()[4].id, id); // justo después del original (índice 3)
        QCOMPARE(f.store.selectedId(), id);
    }

    void moveAndInsertStepsKeepShotAssignments() {
        Fixture f;
        const QString id = QStringLiteral("TC-104"); // 4 pasos
        f.store.addShot(id, Screenshot{1, 1, QStringLiteral("a.png"), {}});
        f.store.addShot(id, Screenshot{2, 3, QStringLiteral("b.png"), {}});
        const QString first = f.store.find(id)->steps[0].action;
        f.store.moveStep(id, 0, +2); // paso 1 → posición 3
        const TestCase* c = f.store.find(id);
        QCOMPARE(c->steps[2].action, first);
        QCOMPARE(c->shots[0].step, 3); // sigue a su paso
        QCOMPARE(c->shots[1].step, 2); // el antiguo paso 3 subió una posición
        f.store.insertStep(id, 1);      // paso vacío en la posición 2
        c = f.store.find(id);
        QCOMPARE(c->steps.size(), 5);
        QVERIFY(c->steps[1].action.isEmpty());
        QCOMPARE(c->shots[0].step, 4);
        QCOMPARE(c->shots[1].step, 3);
        f.store.moveStep(id, 0, -1);    // sin efecto
        QCOMPARE(f.store.find(id)->steps.size(), 5);
    }

    void suitesComeFromCases() {
        Fixture f;
        QCOMPARE(f.store.suites(), (QStringList{QStringLiteral("Autenticación"), QStringLiteral("Checkout"), QStringLiteral("Notificaciones"), QStringLiteral("Perfil")}));
        f.store.updateCase(QStringLiteral("TC-106"), [](TestCase& c) { c.suite = QStringLiteral("Facturación"); });
        QVERIFY(f.store.suites().contains(QStringLiteral("Facturación")));
        QVERIFY(!f.store.suites().contains(QStringLiteral("Perfil"))); // ya nadie la usa
        f.store.select(QStringLiteral("TC-106"));
        const QString id = f.store.createCase();
        QCOMPARE(f.store.find(id)->suite, QStringLiteral("Facturación")); // hereda la del seleccionado
    }

    void mergeCasesAddsAndUpdatesKeepingLocalData() {
        Fixture f;
        f.store.addShot(QStringLiteral("TC-104"), Screenshot{1, 1, QStringLiteral("a.png"), {}});
        TestCase updated = *f.store.find(QStringLiteral("TC-104"));
        updated.title = QStringLiteral("Nuevo título");
        updated.shots.clear();
        updated.lastRun = LastRun{};
        TestCase fresh;
        fresh.id = QStringLiteral("TC-500");
        fresh.title = QStringLiteral("Importado");
        const auto [added, upd] = f.store.mergeCases({updated, fresh});
        QCOMPARE(added, 1);
        QCOMPARE(upd, 1);
        const TestCase* c = f.store.find(QStringLiteral("TC-104"));
        QCOMPARE(c->title, QStringLiteral("Nuevo título"));
        QCOMPARE(c->shots.size(), 1);                                   // capturas locales conservadas
        QCOMPARE(static_cast<int>(c->lastRun.outcome), static_cast<int>(RunOutcome::Passed));
        QVERIFY(f.store.find(QStringLiteral("TC-500")));
        QCOMPARE(f.store.nextCaseId(), QStringLiteral("TC-501"));
    }

    void planDropsDeletedCases() {
        Fixture f;
        PlanStore plan(f.repo, f.store, f.history);
        plan.load();
        QVERIFY(plan.active()->contains(QStringLiteral("TC-104")));
        f.store.removeCase(QStringLiteral("TC-104"));
        QVERIFY(!plan.active()->contains(QStringLiteral("TC-104")));
        QCOMPARE(plan.orderedCaseIds().size(), 3);
    }

    void plansCanBeCreatedArchivedAndRemoved() {
        Fixture f;
        PlanStore plans(f.repo, f.store, f.history);
        plans.load();
        QCOMPARE(plans.plans().size(), 1);
        QCOMPARE(plans.activeId(), QStringLiteral("PL-0001"));
        QCOMPARE(plans.active()->name, QStringLiteral("Regresión Sprint 14"));

        QSignalSpy changed(&plans, &PlanStore::plansChanged);
        const QString id = plans.createPlan(QStringLiteral("Smoke"));
        QCOMPARE(id, QStringLiteral("PL-0002"));
        QCOMPARE(plans.activeId(), id);           // el nuevo pasa a ser el activo
        QVERIFY(plans.orderedCaseIds().isEmpty());
        QVERIFY(changed.count() >= 1);
        plans.toggle(QStringLiteral("TC-103"));
        QCOMPARE(plans.orderedCaseIds(), QStringList{QStringLiteral("TC-103")});

        const QString copy = plans.duplicatePlan(id);
        QCOMPARE(plans.active()->name, QStringLiteral("Smoke (copia)"));
        QCOMPARE(plans.orderedCaseIds(), QStringList{QStringLiteral("TC-103")});

        plans.setArchived(copy, true);
        QVERIFY(plans.find(copy)->archived);
        plans.removePlan(copy);
        QVERIFY(!plans.find(copy));
        QVERIFY(!plans.activeId().isEmpty());
        QVERIFY(plans.find(plans.activeId()));

        // Persistencia: activo y colección viajan juntos.
        QVERIFY(f.repo->plans.has_value());
        QCOMPARE(f.repo->plans->plans.size(), 2);
        QCOMPARE(f.repo->plans->activeId, plans.activeId());
        PlanStore again(f.repo, f.store, f.history);
        again.load();
        QCOMPARE(again.plans().size(), 2);
        QCOMPARE(again.activeId(), plans.activeId());
    }

    void planKeepsItsOwnOrder() {
        Fixture f;
        PlanStore plans(f.repo, f.store, f.history);
        plans.load();
        plans.selectNone();
        plans.toggle(QStringLiteral("TC-105"));
        plans.toggle(QStringLiteral("TC-101"));
        plans.toggle(QStringLiteral("TC-104"));
        QCOMPARE(plans.orderedCaseIds(), (QStringList{QStringLiteral("TC-105"), QStringLiteral("TC-101"), QStringLiteral("TC-104")})); // orden de inserción
        plans.moveCase(QStringLiteral("TC-104"), -2);
        QCOMPARE(plans.orderedCaseIds(), (QStringList{QStringLiteral("TC-104"), QStringLiteral("TC-105"), QStringLiteral("TC-101")}));
        plans.moveCase(QStringLiteral("TC-104"), -1); // sin efecto en el extremo
        QCOMPARE(plans.orderedCaseIds().first(), QStringLiteral("TC-104"));
        plans.sortByPriority(); // TC-105 es Media; TC-104 y TC-101 Alta (orden estable)
        QCOMPARE(plans.orderedCaseIds(), (QStringList{QStringLiteral("TC-104"), QStringLiteral("TC-101"), QStringLiteral("TC-105")}));
        // Los obsoletos no se ejecutan pero siguen en el plan por si vuelven.
        f.store.updateCase(QStringLiteral("TC-101"), [](TestCase& c) { c.status = CaseStatus::Obsoleto; });
        QCOMPARE(plans.orderedCaseIds().size(), 2);
        QVERIFY(plans.active()->contains(QStringLiteral("TC-101")));
    }

    void cyclesAreLinkedToTheirPlan() {
        Fixture f;
        PlanStore plans(f.repo, f.store, f.history);
        plans.load();
        const QString planId = plans.activeId();
        QVERIFY(!plans.latestCycle(planId).has_value());
        QCOMPARE(plans.cycleCount(planId), 0);

        f.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, plans.active()->name, planId);
        auto cycle = plans.latestCycle(planId);
        QVERIFY(cycle.has_value());
        QVERIFY(!cycle->plan.isFinished());
        QCOMPARE(cycle->executed, 0);
        f.run.mark(StepResult::Pass);
        f.run.finish();
        cycle = plans.latestCycle(planId);
        QCOMPARE(cycle->executed, 1);      // progreso del ciclo en curso
        QCOMPARE(cycle->pending(), 1);
        f.run.mark(StepResult::Fail);
        f.run.finish();
        cycle = plans.latestCycle(planId);
        QVERIFY(cycle->plan.isFinished());
        QCOMPARE(cycle->successRate(), 50);
        QCOMPARE(plans.cycleCount(planId), 1);

        // Un segundo ciclo es el nuevo "último"; el otro plan no tiene ciclos.
        f.run.startSequence({QStringLiteral("TC-103")}, plans.active()->name, planId);
        QCOMPARE(plans.cycleCount(planId), 2);
        QCOMPARE(plans.latestCycle(planId)->executed, 0);
        const QString other = plans.createPlan(QStringLiteral("Otro"));
        QVERIFY(!plans.latestCycle(other).has_value());
        QCOMPARE(f.history.findPlan(f.run.planRunId())->planId, planId);
    }

    void planEstimate() {
        Fixture f;
        TestCaseStore& store = f.store;
        auto repo = f.repo;
        PlanStore plan(repo, store, f.history);
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
