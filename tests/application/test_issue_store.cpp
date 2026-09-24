// IssueStore (application/IssueStore.h): alta y edición, los planes que prueban cada requerimiento
// (y los casos que salen de ellos), avance del flujo y revisiones (acta, publicación y registro),
// importación de requerimientos sin duplicados ni pisar lo escrito en QAflow, cambios pendientes,
// requerimientos que salen de la bandeja, resultados de los ciclos de sus planes y datos que no se
// pueden leer.

#include "support/AppFixture.h"
#include "support/MemoryRepositories.h"

#include "application/IssueStore.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;
using qaflow::testing::MemoryIssueRepository;

namespace {
const QString kConnection = QStringLiteral("http://gesreq.test:7401/greq");

ExternalRequirement requirementOf(const QString& id, const QString& state = QStringLiteral("CONTROL CALIDAD ASIGNADO")) {
    ExternalRequirement r;
    r.id = id;
    r.system = QStringLiteral("SUMA TRANSITO-TRANSITOS");
    r.systemCode = QStringLiteral("SUMA TRANSITO");
    r.systemName = QStringLiteral("TRANSITOS");
    r.summary = QStringLiteral("Requerimiento %1").arg(id);
    r.priority = QStringLiteral("ALTA");
    r.states = {state};
    return r;
}

struct Fixture {
    std::shared_ptr<MemoryIssueRepository> repo = std::make_shared<MemoryIssueRepository>();
    IssueStore store{repo};
    Fixture() { store.load(); }
};
} // namespace

class IssueStoreTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Alta y asociaciones --------------------------------------------------------------------
    void createsIssuesWithLocalIdsAndSelectsTheNewOne() {
        Fixture f;
        QSignalSpy selected(&f.store, &IssueStore::selectionChanged);
        const QString a = f.store.createIssue(QStringLiteral("  Revisar login  "));
        const QString b = f.store.createIssue(QString());
        QCOMPARE(a, QStringLiteral("IS-0001"));
        QCOMPARE(b, QStringLiteral("IS-0002"));
        QCOMPARE(f.store.find(a)->title, QStringLiteral("Revisar login"));
        QVERIFY(!f.store.find(b)->title.isEmpty());
        QVERIFY(f.store.find(a)->createdAt.isValid());
        QVERIFY(!f.store.find(a)->isImported());
        QCOMPARE(f.store.selectedId(), b);
        QCOMPARE(selected.count(), 2);
        QCOMPARE(f.repo->issues->size(), 2);   // persiste en cada cambio
    }

    void linksPlansOnceAndAPlanCanCoverSeveralIssues() {
        Fixture f;
        const QString a = f.store.createIssue(QStringLiteral("A"));
        const QString b = f.store.createIssue(QStringLiteral("B"));
        QSignalSpy changed(&f.store, &IssueStore::issueChanged);
        f.store.linkPlan(a, QStringLiteral("PL-0001"));
        f.store.linkPlan(a, QStringLiteral("PL-0001"));
        f.store.linkPlan(b, QStringLiteral("PL-0001"));
        QCOMPARE(f.store.find(a)->planIds, QStringList{QStringLiteral("PL-0001")});
        QCOMPARE(f.store.issuesForPlan(QStringLiteral("PL-0001")).size(), 2);
        QCOMPARE(changed.count(), 2);   // el vínculo repetido no cambia nada
        f.store.unlinkPlan(a, QStringLiteral("PL-0001"));
        QCOMPARE(f.store.issuesForPlan(QStringLiteral("PL-0001")).size(), 1);
        QVERIFY(f.store.find(a)->planIds.isEmpty());
    }

    // Los casos del issue no se vinculan uno a uno: son los de sus planes, en su orden y sin repetir.
    void itsCasesAreTheOnesInItsPlans() {
        AppFixture f;
        const QString id = f.issues.createIssue(QStringLiteral("Checkout"));
        QVERIFY(IssueStore::caseIdsOf(*f.issues.find(id), f.plans).isEmpty());

        f.issues.linkPlan(id, QStringLiteral("PL-0001"));
        const QStringList cases = IssueStore::caseIdsOf(*f.issues.find(id), f.plans);
        QCOMPARE(cases, f.plans.orderedCaseIds(QStringLiteral("PL-0001")));
        QVERIFY(cases.contains(QStringLiteral("TC-101")));

        const QString other = f.plans.createPlan(QStringLiteral("Humo"));
        f.plans.toggle(QStringLiteral("TC-101"));   // repetido: no sale dos veces
        f.plans.toggle(QStringLiteral("TC-106"));
        f.issues.linkPlan(id, other);
        const QStringList both = IssueStore::caseIdsOf(*f.issues.find(id), f.plans);
        QCOMPARE(both.size(), cases.size() + 1);
        QCOMPARE(both.last(), QStringLiteral("TC-106"));
    }

    // ---- Flujo y revisiones ---------------------------------------------------------------------
    void theFirstPlanMovesThePendingIssueToPreparing() {
        Fixture f;
        const QString id = f.store.createIssue(QStringLiteral("A"));
        QVERIFY(f.store.find(id)->state == IssueState::Pending);
        f.store.linkPlan(id, QStringLiteral("PL-0001"));
        QVERIFY(f.store.find(id)->state == IssueState::Preparing);

        // Un estado más avanzado no retrocede al vincular otro plan.
        f.store.updateIssue(id, [](Issue& i) { i.state = IssueState::Testing; });
        f.store.linkPlan(id, QStringLiteral("PL-0002"));
        QVERIFY(f.store.find(id)->state == IssueState::Testing);
    }

    void startingACycleOfItsPlanOpensTheRevisionAndPutsItInTesting() {
        Fixture f;
        const QString id = f.store.createIssue(QStringLiteral("A"));
        const QString other = f.store.createIssue(QStringLiteral("B"));
        f.store.linkPlan(id, QStringLiteral("PL-0001"));
        f.store.linkPlan(other, QStringLiteral("PL-0002"));

        f.store.notePlanStarted(QStringLiteral("PL-0001"));
        const Issue* issue = f.store.find(id);
        QVERIFY(issue->state == IssueState::Testing);
        QCOMPARE(issue->revisions.size(), 1);
        QCOMPARE(issue->currentRevision()->number, 1);
        QVERIFY(f.store.find(other)->revisions.isEmpty());   // su plan no arrancó

        // Otro ciclo de la misma ronda no abre una revisión nueva.
        f.store.notePlanStarted(QStringLiteral("PL-0001"));
        QCOMPARE(f.store.find(id)->revisions.size(), 1);

        // Cerrada la revisión, volver a probar abre la siguiente.
        f.store.closeRevision(id, QaOutcome::Observado);
        QVERIFY(f.store.find(id)->state == IssueState::Testing);   // observado no es terminado
        f.store.notePlanStarted(QStringLiteral("PL-0001"));
        issue = f.store.find(id);
        QCOMPARE(issue->revisions.size(), 2);
        QCOMPARE(issue->currentRevision()->number, 2);
        QVERIFY(issue->state == IssueState::Testing);
        QVERIFY(issue->lastOutcome() == QaOutcome::Observado);   // el de la revisión ya cerrada
    }

    void eachRoundIsOfAPhaseAndOnlyTheLastOneFinishesTheIssue() {
        Fixture f;
        const QString id = f.store.createIssue(QStringLiteral("A"));
        f.store.linkPlan(id, QStringLiteral("PL-0001"));
        const QString plan = QStringLiteral("PL-0001");

        // Lo que haría un ciclo antes de arrancarlo: la revisión 1, en QA.
        IssueStore::RevisionRef next = f.store.nextCycleContext(plan);
        QCOMPARE(next.issueId, id);
        QCOMPARE(next.revision, 1);
        QCOMPARE(next.phase, QStringLiteral("QA"));
        IssueStore::RevisionRef started = f.store.notePlanStarted(plan);
        QCOMPARE(started.revision, 1);
        QCOMPARE(started.phase, QStringLiteral("QA"));
        QCOMPARE(f.store.find(id)->currentRevision()->phase, QStringLiteral("QA"));

        // QA observada: se corrige y se vuelve a probar en QA.
        f.store.closeRevision(id, QaOutcome::Observado);
        QCOMPARE(f.store.nextCycleContext(plan).phase, QStringLiteral("QA"));
        started = f.store.notePlanStarted(plan);
        QCOMPARE(started.revision, 2);
        QCOMPARE(started.phase, QStringLiteral("QA"));

        // QA aprobada: el issue sigue en pruebas y la ronda siguiente es de PRE.
        f.store.closeRevision(id, QaOutcome::Conforme);
        QVERIFY(f.store.find(id)->state == IssueState::Testing);
        next = f.store.nextCycleContext(plan);
        QCOMPARE(next.revision, 3);
        QCOMPARE(next.phase, QStringLiteral("PRE"));
        QCOMPARE(f.store.openRevision(id), 3);
        QCOMPARE(f.store.find(id)->currentRevision()->phase, QStringLiteral("PRE"));

        // PRE conforme: es el cierre del control.
        f.store.closeRevision(id, QaOutcome::Conforme);
        QVERIFY(f.store.find(id)->state == IssueState::Done);
    }

    // Hay requerimientos que sólo se prueban en una fase: en sólo PRE la primera ronda ya es de PRE, y
    // en sólo QA su Conforme es el cierre del control.
    void anIssueCanBeTestedInOnlyOnePhase() {
        Fixture f;
        const QString pre = f.store.createIssue(QStringLiteral("Sólo PRE"));
        QVERIFY(f.store.setIssuePhases(pre, {QStringLiteral("PRE")}).isEmpty());
        QCOMPARE(f.store.phasesOf(*f.store.find(pre)), QStringList{QStringLiteral("PRE")});
        f.store.openRevision(pre);
        QCOMPARE(f.store.find(pre)->currentRevision()->phase, QStringLiteral("PRE"));
        f.store.closeRevision(pre, QaOutcome::Conforme);
        QVERIFY(f.store.find(pre)->state == IssueState::Done);

        const QString qa = f.store.createIssue(QStringLiteral("Sólo QA"));
        QVERIFY(f.store.setIssuePhases(qa, {QStringLiteral("qa")}).isEmpty());
        f.store.openRevision(qa);
        QCOMPARE(f.store.find(qa)->currentRevision()->phase, QStringLiteral("QA"));
        f.store.closeRevision(qa, QaOutcome::Conforme);
        QVERIFY(f.store.find(qa)->state == IssueState::Done);
        // Con una revisión cerrada en QA, esa fase ya no se puede quitar.
        QVERIFY(!f.store.setIssuePhases(qa, {QStringLiteral("PRE")}).isEmpty());
        QCOMPARE(f.store.phasesOf(*f.store.find(qa)), QStringList{QStringLiteral("QA")});

        // Una revisión abierta (sin ciclos) no ata su fase: quitada, la revisión pasa a la que queda.
        const QString moved = f.store.createIssue(QStringLiteral("Empezó en QA por error"));
        f.store.openRevision(moved);
        QCOMPARE(f.store.find(moved)->currentRevision()->phase, QStringLiteral("QA"));
        QVERIFY(f.store.setIssuePhases(moved, {QStringLiteral("PRE")}).isEmpty());
        QCOMPARE(f.store.find(moved)->currentRevision()->phase, QStringLiteral("PRE"));

        // Elegir todas las del proyecto vuelve a «las del proyecto».
        const QString both = f.store.createIssue(QStringLiteral("Las dos"));
        QVERIFY(f.store.setIssuePhases(both, {QStringLiteral("PRE"), QStringLiteral("QA")}).isEmpty());
        QVERIFY(f.store.find(both)->phases.isEmpty());
    }

    // El ciclo se arranca en la fase que se elige: una revisión nueva nace en ella y una abierta (sin
    // ciclos todavía) pasa a ella. Una fase que no es del issue no cuenta.
    void aCycleStartsInTheChosenPhase() {
        Fixture f;
        const QString id = f.store.createIssue(QStringLiteral("A"));
        f.store.linkPlan(id, QStringLiteral("PL-0001"));
        IssueStore::RevisionRef started = f.store.notePlanStarted(QStringLiteral("PL-0001"), QStringLiteral("pre"));
        QCOMPARE(started.phase, QStringLiteral("PRE"));
        QCOMPARE(f.store.find(id)->currentRevision()->phase, QStringLiteral("PRE"));
        started = f.store.notePlanStarted(QStringLiteral("PL-0001"), QStringLiteral("QA"));
        QCOMPARE(started.revision, 1);
        QCOMPARE(f.store.find(id)->currentRevision()->phase, QStringLiteral("QA"));
        started = f.store.notePlanStarted(QStringLiteral("PL-0001"), QStringLiteral("Staging"));
        QCOMPARE(started.phase, QStringLiteral("QA"));   // no es una fase: sigue en la suya
    }

    void aProjectCanHaveItsOwnPhases() {
        Fixture f;
        f.store.setPhases({QStringLiteral("QA"), QStringLiteral("UAT"), QStringLiteral("PRE")});
        const QString id = f.store.createIssue(QStringLiteral("A"));
        f.store.openRevision(id);
        f.store.closeRevision(id, QaOutcome::Conforme);
        f.store.openRevision(id);
        QCOMPARE(f.store.find(id)->currentRevision()->phase, QStringLiteral("UAT"));
        f.store.closeRevision(id, QaOutcome::Conforme);
        QVERIFY(f.store.find(id)->state == IssueState::Testing);
        f.store.openRevision(id);
        QCOMPARE(f.store.find(id)->currentRevision()->phase, QStringLiteral("PRE"));
    }

    void theRevisionKeepsTheRecordThePublicationAndTheRegistration() {
        Fixture f;
        const QString id = f.store.createIssue(QStringLiteral("A"));
        QCOMPARE(f.store.openRevision(id), 1);
        QCOMPARE(f.store.openRevision(id), 1);   // ya estaba abierta

        QualityRecord record;
        record.greq = QStringLiteral("2026997");
        record.revisionNumber = 1;
        f.store.setRevisionRecord(id, record, QStringLiteral("/tmp/ControlCalidad_2026997.docx"));
        const IssueRevision* revision = f.store.find(id)->currentRevision();
        QCOMPARE(revision->record.greq, QStringLiteral("2026997"));
        QVERIFY(revision->hasDocument());
        QVERIFY(revision->documentAt.isValid());

        RevisionPublication published;
        published.key = QStringLiteral("SUMA2-2907");
        published.publishedAt = QDateTime::currentDateTime();
        published.attachedDocument = true;
        f.store.setRevisionPublication(id, published);
        QCOMPARE(f.store.find(id)->currentRevision()->jira.key, QStringLiteral("SUMA2-2907"));

        RevisionRegistration registered;
        registered.registeredAt = QDateTime::currentDateTime();
        registered.result = QaOutcome::Conforme;
        f.store.setRevisionRegistration(id, registered);
        QVERIFY(f.store.find(id)->currentRevision()->gesreq.result == QaOutcome::Conforme);

        // Un proyecto de una sola fase: su Conforme cierra el control y finaliza el issue.
        f.store.setPhases({QStringLiteral("QA")});
        f.store.closeRevision(id, QaOutcome::Conforme);
        const Issue* issue = f.store.find(id);
        QVERIFY(!issue->currentRevision());
        QVERIFY(issue->state == IssueState::Done);
        QVERIFY(issue->lastOutcome() == QaOutcome::Conforme);
        // Con la revisión cerrada, el acta se puede regenerar sin reabrirla.
        f.store.setRevisionRecord(id, record, QStringLiteral("/tmp/otra.docx"));
        QCOMPARE(f.store.find(id)->revisions.size(), 1);
        QCOMPARE(f.store.find(id)->revisions.last().documentPath, QStringLiteral("/tmp/otra.docx"));
    }

    void removingAnIssueSelectsTheNextOne() {
        Fixture f;
        const QString a = f.store.createIssue(QStringLiteral("A"));
        const QString b = f.store.createIssue(QStringLiteral("B"));
        f.store.select(a);
        f.store.removeIssue(a);
        QVERIFY(!f.store.find(a));
        QCOMPARE(f.store.selectedId(), b);
        f.store.removeIssue(b);
        QVERIFY(f.store.selectedId().isEmpty());
    }

    // ---- Importación ----------------------------------------------------------------------------
    void importCreatesOneIssuePerRequirementWithItsDataAsStartingPoint() {
        Fixture f;
        const IssueStore::ImportResult result = f.store.importRequirements(
            {requirementOf(QStringLiteral("2025175")), requirementOf(QStringLiteral("2026310")), requirementOf(QStringLiteral("2025175"))},
            kConnection + QStringLiteral("/"));
        QCOMPARE(result.created.size(), 2);   // el repetido en la misma lectura no duplica
        QVERIFY(result.updated.isEmpty());
        const Issue* issue = f.store.findByRequirement(kConnection, QStringLiteral("2025175"));
        QVERIFY(issue);
        QCOMPARE(issue->title, QStringLiteral("Requerimiento 2025175"));
        QVERIFY(issue->priority == Priority::Alta);
        QVERIFY(issue->state == IssueState::Pending);
        QCOMPARE(issue->requirement.connection, kConnection);   // sin la barra final
        QCOMPARE(issue->requirement.data.systemCode, QStringLiteral("SUMA TRANSITO"));
        QVERIFY(issue->requirement.importedAt.isValid());
        QVERIFY(!issue->requirement.missing);
        QCOMPARE(f.store.selectedId(), result.created.first());
    }

    void reimportingNeverDuplicatesAndKeepsWhatWasWrittenInQAflow() {
        Fixture f;
        const ExternalRequirement original = requirementOf(QStringLiteral("2025175"));
        f.store.importRequirements({original}, kConnection);
        const QString id = f.store.issues().first().id;
        f.store.linkPlan(id, QStringLiteral("PL-0001"));
        f.store.updateIssue(id, [](Issue& i) {
            i.title = QStringLiteral("Mi título");
            i.notes = QStringLiteral("Probar con el usuario de aduana");
            i.priority = Priority::Baja;
            i.state = IssueState::Testing;
        });

        ExternalRequirement changed = original;
        changed.states = {QStringLiteral("CONTROL DE CALIDAD OBSERVADO")};
        changed.summary = QStringLiteral("Nuevo resumen");
        const auto preview = f.store.previewImport({changed, requirementOf(QStringLiteral("2026999"))}, QStringLiteral("HTTP://GESREQ.TEST:7401/greq"));
        QCOMPARE(preview.size(), 2);
        QVERIFY(preview[0].kind == IssueStore::ImportCandidate::Kind::Changed);
        QCOMPARE(preview[0].issueId, id);
        QCOMPARE(preview[0].changes.size(), 2);
        QVERIFY(preview[1].kind == IssueStore::ImportCandidate::Kind::New);

        const IssueStore::ImportResult result = f.store.importRequirements({changed}, kConnection);
        QCOMPARE(f.store.issues().size(), 1);
        QCOMPARE(result.updated, QStringList{id});
        const Issue* issue = f.store.find(id);
        QCOMPARE(issue->title, QStringLiteral("Mi título"));
        QCOMPARE(issue->notes, QStringLiteral("Probar con el usuario de aduana"));
        QVERIFY(issue->priority == Priority::Baja);
        QVERIFY(issue->state == IssueState::Testing);
        QCOMPARE(issue->planIds, QStringList{QStringLiteral("PL-0001")});
        QCOMPARE(issue->requirement.data.summary, QStringLiteral("Nuevo resumen"));
        QCOMPARE(issue->requirement.changes.size(), 2);
        QCOMPARE(f.store.changedCount(), 1);

        // La misma lectura otra vez no trae nada nuevo; en otra conexión es otro requerimiento.
        QVERIFY(f.store.previewImport({changed}, kConnection).first().kind == IssueStore::ImportCandidate::Kind::Unchanged);
        QVERIFY(f.store.importRequirements({changed}, kConnection).updated.isEmpty());
        QVERIFY(f.store.previewImport({changed}, QStringLiteral("http://otro:7401/greq")).first().kind == IssueStore::ImportCandidate::Kind::New);

        f.store.acknowledgeChanges(id);
        QVERIFY(f.store.find(id)->requirement.changes.isEmpty());
        QCOMPARE(f.store.changedCount(), 0);
    }

    // Iniciar las pruebas de un requerimiento lo abre: la primera vez crea su issue y después reutiliza
    // el mismo, con sus planes y lo escrito en QAflow.
    void startingTestsOpensTheIssueOfTheRequirementAndReusesIt() {
        Fixture f;
        const ExternalRequirement requirement = requirementOf(QStringLiteral("2025175"));
        const QString id = f.store.openForRequirement(requirement, kConnection);
        QVERIFY(!id.isEmpty());
        QCOMPARE(f.store.issues().size(), 1);
        QCOMPARE(f.store.selectedId(), id);
        f.store.linkPlan(id, QStringLiteral("PL-0001"));
        f.store.updateIssue(id, [](Issue& i) {
            i.title = QStringLiteral("Mi título");
            i.state = IssueState::Testing;
        });
        f.store.createIssue(QStringLiteral("Suelto"));   // la selección se va a otro issue

        ExternalRequirement changed = requirement;
        changed.states = {QStringLiteral("CONTROL DE CALIDAD OBSERVADO")};
        QCOMPARE(f.store.openForRequirement(changed, kConnection + QStringLiteral("/")), id);
        QCOMPARE(f.store.issues().size(), 2);   // no duplica
        QCOMPARE(f.store.selectedId(), id);     // y lo deja delante para empezar
        const Issue* issue = f.store.find(id);
        QCOMPARE(issue->title, QStringLiteral("Mi título"));
        QCOMPARE(issue->planIds, QStringList{QStringLiteral("PL-0001")});
        QVERIFY(issue->state == IssueState::Testing);
        QCOMPARE(issue->requirement.data.states, QStringList{QStringLiteral("CONTROL DE CALIDAD OBSERVADO")});
        QVERIFY(f.store.openForRequirement(ExternalRequirement{}, kConnection).isEmpty());
    }

    // Que un requerimiento deje de estar en la bandeja no borra el issue ni sus pruebas.
    void requirementsThatLeaveTheInboxAreKeptAndMarkedAsMissing() {
        Fixture f;
        const ExternalRequirement a = requirementOf(QStringLiteral("2025175"));
        const ExternalRequirement b = requirementOf(QStringLiteral("2026310"));
        f.store.importRequirements({a, b}, kConnection);
        const QString idA = f.store.findByRequirement(kConnection, a.id)->id;
        f.store.linkPlan(idA, QStringLiteral("PL-0001"));
        const QString manual = f.store.createIssue(QStringLiteral("A mano"));

        QCOMPARE(f.store.markInboxRead({b}, kConnection), 1);
        QVERIFY(f.store.find(idA)->requirement.missing);
        QCOMPARE(f.store.find(idA)->planIds, QStringList{QStringLiteral("PL-0001")});
        QVERIFY(!f.store.findByRequirement(kConnection, b.id)->requirement.missing);
        QVERIFY(!f.store.find(manual)->requirement.missing);

        QCOMPARE(f.store.markInboxRead({a, b}, kConnection), 0);   // vuelve a estar
        QVERIFY(!f.store.find(idA)->requirement.missing);
        QCOMPARE(f.store.markInboxRead({}, QStringLiteral("http://otro:7401/greq")), 0);   // otra conexión no los toca
        QVERIFY(!f.store.find(idA)->requirement.missing);
    }

    void storesTheDetailOfTheRequirement() {
        Fixture f;
        f.store.importRequirements({requirementOf(QStringLiteral("2025175"))}, kConnection);
        const QString id = f.store.issues().first().id;
        RequirementDetail detail;
        detail.id = QStringLiteral("2025175");
        detail.description = QStringLiteral("Alcance");
        f.store.setRequirementDetail(id, detail);
        QCOMPARE(f.store.find(id)->requirement.detail.description, QStringLiteral("Alcance"));
        QVERIFY(f.store.find(id)->requirement.detailFetchedAt.isValid());
        QCOMPARE(f.repo->issues->first().requirement.detail.description, QStringLiteral("Alcance"));
    }

    // ---- Resultados -----------------------------------------------------------------------------
    // Los resultados del issue son los de los ciclos de sus planes: una ejecución suelta del mismo
    // caso, o dentro de otro plan, no es un resultado suyo.
    void itsResultsAreOnlyTheOnesOfItsPlanCycles() {
        AppFixture f;
        const QString id = f.issues.createIssue(QStringLiteral("Checkout"));
        f.issues.linkPlan(id, QStringLiteral("PL-0001"));
        QVERIFY(IssueStore::runsOf(*f.issues.find(id), f.history).isEmpty());

        // Una ejecución suelta de un caso del plan: no es del issue.
        f.run.start(QStringLiteral("TC-101"));
        while (!f.run.state().finished) f.run.mark(StepResult::Pass);
        f.run.finish();
        QVERIFY(IssueStore::runsOf(*f.issues.find(id), f.history).isEmpty());

        // Un ciclo de otro plan con los mismos casos tampoco.
        const QString other = f.plans.createPlan(QStringLiteral("Humo"));
        f.plans.toggle(QStringLiteral("TC-101"));
        f.run.startSequence({QStringLiteral("TC-101")}, QStringLiteral("Humo"), other);
        while (!f.run.state().finished) f.run.mark(StepResult::Pass);
        f.run.finish();
        QVERIFY(IssueStore::runsOf(*f.issues.find(id), f.history).isEmpty());

        // El ciclo del plan del issue sí.
        f.run.startSequence({QStringLiteral("TC-101"), QStringLiteral("TC-102")}, QStringLiteral("Regresión Sprint 14"),
                            QStringLiteral("PL-0001"));
        while (!f.run.state().finished) f.run.mark(StepResult::Pass);
        f.run.finish();
        while (!f.run.state().finished) f.run.mark(StepResult::Fail);
        f.run.finish();

        const QList<PlanRun> cycles = IssueStore::cyclesOf(*f.issues.find(id), f.history);
        QCOMPARE(cycles.size(), 1);
        QCOMPARE(cycles.first().planId, QStringLiteral("PL-0001"));
        const QList<RunRecord> runs = IssueStore::runsOf(*f.issues.find(id), f.history);
        QCOMPARE(runs.size(), 2);
        QCOMPARE(runs[0].caseId, QStringLiteral("TC-102"));   // la más reciente primero
        QVERIFY(runs[0].verdict == Verdict::Fallido);
        QCOMPARE(runs[1].caseId, QStringLiteral("TC-101"));

        // Con `since` sólo cuentan los ciclos de la ronda en curso.
        QVERIFY(IssueStore::runsOf(*f.issues.find(id), f.history, QDateTime::currentDateTime().addSecs(60)).isEmpty());
    }

    // Cada ciclo dice de qué ronda es desde que se arranca, así que los resultados de una revisión no
    // se mezclan con los de la siguiente aunque las dos prueben los mismos planes.
    void eachCycleBelongsToTheRevisionItWasStartedIn() {
        AppFixture f;
        const QString id = f.issues.createIssue(QStringLiteral("Checkout"));
        f.issues.linkPlan(id, QStringLiteral("PL-0001"));

        // Ronda 1: el ciclo la abre y queda anotado en ella (como hace la sesión al arrancarlo).
        const IssueStore::RevisionRef first = f.issues.notePlanStarted(QStringLiteral("PL-0001"));
        QCOMPARE(first.issueId, id);
        QCOMPARE(first.revision, 1);
        const QString cycle1 = f.history.startPlan(QStringLiteral("Regresión"), {QStringLiteral("TC-101")},
                                                   QStringLiteral("PL-0001"), QStringLiteral("QA"));
        f.history.noteCycleRevision(cycle1, first.issueId, first.revision);
        f.issues.closeRevision(id, QaOutcome::Observado);

        // Ronda 2: otro ciclo del mismo plan, en otro ambiente.
        const IssueStore::RevisionRef second = f.issues.notePlanStarted(QStringLiteral("PL-0001"));
        QCOMPARE(second.revision, 2);
        const QString cycle2 = f.history.startPlan(QStringLiteral("Regresión"), {QStringLiteral("TC-101")},
                                                   QStringLiteral("PL-0001"), QStringLiteral("Staging"));
        f.history.noteCycleRevision(cycle2, second.issueId, second.revision);

        const Issue* issue = f.issues.find(id);
        QCOMPARE(IssueStore::cyclesOf(*issue, f.history).size(), 2);
        const QList<PlanRun> round1 = IssueStore::cyclesOfRevision(*issue, f.history, 1);
        QCOMPARE(round1.size(), 1);
        QCOMPARE(round1.first().id, cycle1);
        QCOMPARE(round1.first().environment, QStringLiteral("QA"));
        const QList<PlanRun> round2 = IssueStore::cyclesOfRevision(*issue, f.history, 2);
        QCOMPARE(round2.size(), 1);
        QCOMPARE(round2.first().id, cycle2);
        QCOMPARE(round2.first().environment, QStringLiteral("Staging"));
    }

    // ---- Datos que no se pueden leer ------------------------------------------------------------
    void unreadableIssuesAreNeverOverwritten() {
        auto repo = std::make_shared<MemoryIssueRepository>();
        repo->issues = std::nullopt;
        IssueStore store(repo);
        QSignalSpy loadFailed(&store, &IssueStore::loadFailed);
        QSignalSpy saveFailed(&store, &IssueStore::saveFailed);
        store.load();
        QCOMPARE(loadFailed.count(), 1);
        QVERIFY(store.isReadOnly());
        store.createIssue(QStringLiteral("Se queda en memoria"));
        QCOMPARE(repo->saves, 0);
        QVERIFY(saveFailed.count() >= 1);
        QVERIFY(!store.save());
    }
};

QTEST_MAIN(IssueStoreTest)
#include "test_issue_store.moc"
