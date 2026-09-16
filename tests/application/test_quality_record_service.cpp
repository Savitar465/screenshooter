// QualityRecordService (application/QualityRecordService.h): el acta que se propone para la revisión
// en curso a partir de los ciclos de los planes del issue (con cuál se levanta cuando hay varios), lo
// que se hereda del acta anterior, lo que corrigió el usuario, la generación del fichero y el resumen
// que se manda a Jira y a GESREQ.

#include "support/AppFixture.h"

#include "application/QualityRecordService.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;

namespace {

const QString kConnection = QStringLiteral("http://gesreq.test:7401/greq");

ExternalRequirement requirement() {
    ExternalRequirement r;
    r.id = QStringLiteral("2026997");
    r.system = QStringLiteral("SUMA2-INGRESO");
    r.systemCode = QStringLiteral("SUMA2");
    r.summary = QStringLiteral("Integración de nuevos servicios");
    r.priority = QStringLiteral("ALTA");
    r.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
    return r;
}

/// El issue importado y el plan con el que se prueba (dos casos), ya en pruebas (revisión 1 abierta).
struct Testing {
    QString issueId;
    QString planId;
};

Testing issueInTesting(AppFixture& f) {
    f.issues.importRequirements({requirement()}, kConnection);
    const QString id = f.issues.issues().first().id;
    const QString planId = f.plans.createPlan(QStringLiteral("Plan GREQ 2026997"));
    f.plans.toggle(QStringLiteral("TC-101"));
    f.plans.toggle(QStringLiteral("TC-102"));
    f.issues.linkPlan(id, planId);
    f.issues.notePlanStarted(planId);
    return {id, planId};
}

/// Añade al historial un ciclo de ese plan con sus resultados y en esa fecha (así se pueden colocar
/// ciclos antes y después de que empiece la revisión) y devuelve su id.
QString addCycle(AppFixture& f, const QString& planId, const QString& name, const QDateTime& startedAt,
                 const QList<QPair<QString, Verdict>>& results, const QString& zephyrCycleId = QString()) {
    RunHistory history = f.historyRepo->history.value_or(RunHistory{});
    PlanRun plan;
    plan.id = QStringLiteral("PR-%1").arg(history.plans.size() + 1);
    plan.planId = planId;
    plan.name = name;
    plan.startedAt = startedAt;
    plan.finishedAt = startedAt.addSecs(3600);
    plan.zephyrCycleId = zephyrCycleId;
    if (!zephyrCycleId.isEmpty()) plan.publishedAt = startedAt.addSecs(4000);
    int n = history.runs.size();
    for (const auto& [caseId, verdict] : results) {
        plan.caseIds << caseId;
        RunRecord run;
        run.id = QStringLiteral("R-%1").arg(++n);
        run.caseId = caseId;
        run.caseTitle = QStringLiteral("Caso %1").arg(caseId);
        run.planRunId = plan.id;
        run.verdict = verdict;
        run.startedAt = startedAt.addSecs(60);
        run.finishedAt = startedAt.addSecs(600);
        history.runs << run;
    }
    history.plans << plan;
    f.historyRepo->history = history;
    f.history.load();
    return plan.id;
}

IssueLink bugOf(const QString& key, const QString& caseId, const QString& classification, const QDateTime& when, bool resolved = false) {
    IssueLink bug;
    bug.key = key;
    bug.url = QStringLiteral("http://jira.test/browse/%1").arg(key);
    bug.title = QStringLiteral("Falla");
    bug.caseId = caseId;
    bug.classification = classification;
    bug.resolved = resolved;
    bug.createdAt = when;
    return bug;
}

} // namespace

class QualityRecordServiceTest : public QObject {
    Q_OBJECT
private slots:
    void theDraftOnlyCountsWhatThisRevisionExecuted() {
        AppFixture f;
        const Testing t = issueInTesting(f);
        // Un ciclo de la ronda anterior (antes de que se abriera la revisión) no cuenta.
        addCycle(f, t.planId, QStringLiteral("Ronda anterior"), QDateTime::currentDateTime().addDays(-10),
                 {{QStringLiteral("TC-101"), Verdict::Fallido}});
        addCycle(f, t.planId, QStringLiteral("Plan GREQ 2026997"), QDateTime::currentDateTime(),
                 {{QStringLiteral("TC-101"), Verdict::Superado}, {QStringLiteral("TC-102"), Verdict::Fallido}});

        const QualityRecord draft = f.records.draftFor(t.issueId);
        QCOMPARE(draft.greq, QStringLiteral("2026997"));
        QCOMPARE(draft.system, QStringLiteral("SUMA2-INGRESO"));
        QCOMPARE(draft.revisionNumber, 1);
        QVERIFY(draft.execution.contains(QStringLiteral("2 de 2 ejecutados: 1 superado(s), 1 fallido(s), 0 bloqueado(s)")));
        QCOMPARE(f.records.revisionRuns(*f.issues.find(t.issueId)).size(), 2);   // el ciclo viejo no entra
        QCOMPARE(f.records.cyclesFor(t.issueId).size(), 1);

        const IssueProgress progress = f.records.progressFor(t.issueId);
        QCOMPARE(progress.cases, 2);
        QCOMPARE(progress.executed, 2);
        QCOMPARE(progress.failed, 1);
        QVERIFY(progress.suggested == QaOutcome::Observado);
        QVERIFY(progress.canClose());
    }

    // Con varias ejecuciones en la misma revisión, el acta se levanta con la que se elija.
    void theRecordIsRaisedWithTheChosenCycle() {
        AppFixture f;
        const Testing t = issueInTesting(f);
        const QDateTime now = QDateTime::currentDateTime();
        const QString first = addCycle(f, t.planId, QStringLiteral("Primera pasada"), now.addSecs(60),
                                       {{QStringLiteral("TC-101"), Verdict::Fallido}});
        const QString second = addCycle(f, t.planId, QStringLiteral("Repetición"), now.addSecs(3600),
                                        {{QStringLiteral("TC-101"), Verdict::Superado}, {QStringLiteral("TC-102"), Verdict::Superado}},
                                        QStringLiteral("42"));

        const QList<PlanReport> cycles = f.records.cyclesFor(t.issueId);
        QCOMPARE(cycles.size(), 2);
        QCOMPARE(cycles.first().plan.id, second);            // la más reciente primero
        QCOMPARE(f.records.recordCycleFor(t.issueId), second);   // y es con la que se propone el acta

        const QualityRecord latest = f.records.draftFor(t.issueId, second);
        QVERIFY(latest.execution.contains(QStringLiteral("Repetición")));
        QVERIFY(!latest.execution.contains(QStringLiteral("Primera pasada")));
        QVERIFY(latest.execution.contains(QStringLiteral("2 de 2 ejecutados: 2 superado(s)")));

        const QualityRecord older = f.records.draftFor(t.issueId, first);
        QVERIFY(older.execution.contains(QStringLiteral("Primera pasada")));
        QVERIFY(older.execution.contains(QStringLiteral("1 de 1 ejecutados: 0 superado(s), 1 fallido(s)")));

        // Sin elegir ninguna, el acta habla de todas las ejecuciones de la revisión.
        const QualityRecord all = f.records.draftFor(t.issueId);
        QVERIFY(all.execution.contains(QStringLiteral("Primera pasada")));
        QVERIFY(all.execution.contains(QStringLiteral("Repetición")));

        // El ciclo elegido queda en la revisión y se conserva al regenerarla.
        QVERIFY(f.records.generate(t.issueId, latest, QStringLiteral("/tmp/acta.docx"), second).ok);
        QCOMPARE(f.issues.find(t.issueId)->currentRevision()->planRunId, second);
        QCOMPARE(f.records.recordCycleFor(t.issueId), second);
    }

    void bugsOfTheRevisionAreObservationsAndTheOlderFixedOnesAreCorrections() {
        AppFixture f;
        const QDateTime old = QDateTime::currentDateTime().addDays(-5);
        f.bugLedger.recordIssue(bugOf(QStringLiteral("SHOP-1"), QStringLiteral("TC-101"), QStringLiteral("A"), old, true));
        f.bugLedger.recordIssue(bugOf(QStringLiteral("SHOP-2"), QStringLiteral("TC-101"), QStringLiteral("B"), old, false));
        const Testing t = issueInTesting(f);
        const QDateTime now = QDateTime::currentDateTime();
        addCycle(f, t.planId, QStringLiteral("Plan GREQ 2026997"), now, {{QStringLiteral("TC-101"), Verdict::Fallido}});
        f.bugLedger.recordIssue(bugOf(QStringLiteral("SHOP-3"), QStringLiteral("TC-101"), QStringLiteral("A"), now));
        f.bugLedger.recordIssue(bugOf(QStringLiteral("SHOP-4"), QStringLiteral("TC-102"), QStringLiteral("C"), now));

        const QualityRecord draft = f.records.draftFor(t.issueId);
        QCOMPARE(draft.observations[0].observations, 1);   // A, de esta ronda
        QCOMPARE(draft.observations[2].observations, 1);   // C
        QCOMPARE(draft.totalObservations(), 2);
        QCOMPARE(draft.observations[0].corrections, 1);    // el A de la ronda anterior, ya cerrado
        QCOMPARE(draft.observations[1].corrections, 0);    // el B sigue abierto
        QVERIFY(draft.bugs.contains(QStringLiteral("SHOP-3")));
        QVERIFY(!draft.bugs.contains(QStringLiteral("SHOP-1")));
    }

    void generatingTheRecordSavesItInTheRevisionAndTheNextDraftKeepsIt() {
        AppFixture f;
        const Testing t = issueInTesting(f);
        addCycle(f, t.planId, QStringLiteral("Plan GREQ 2026997"), QDateTime::currentDateTime(),
                 {{QStringLiteral("TC-101"), Verdict::Superado}});

        QualityRecord record = f.records.draftFor(t.issueId);
        record.server = QStringLiteral("10.0.67.131");
        record.dbSchema = QStringLiteral("SUMA2");
        record.generalNotes = QStringLiteral("Sin observaciones");
        record.characteristics[0].satisfied = false;
        const QString path = QStringLiteral("/tmp/ControlCalidad_2026997.docx");
        const auto result = f.records.generate(t.issueId, record, path);
        QVERIFY(result.ok);
        QCOMPARE(result.path, path);
        QCOMPARE(f.recordWriter->calls, 1);
        QCOMPARE(f.recordWriter->lastRecord.server, QStringLiteral("10.0.67.131"));

        const IssueRevision* revision = f.issues.find(t.issueId)->currentRevision();
        QVERIFY(revision);
        QCOMPARE(revision->documentPath, path);
        QCOMPARE(revision->record.dbSchema, QStringLiteral("SUMA2"));

        // Volver a pedir el borrador conserva lo corregido y actualiza lo que sale del proyecto.
        addCycle(f, t.planId, QStringLiteral("Segunda pasada"), QDateTime::currentDateTime().addSecs(3600),
                 {{QStringLiteral("TC-101"), Verdict::Superado}, {QStringLiteral("TC-102"), Verdict::Fallido}});
        const QualityRecord again = f.records.draftFor(t.issueId);
        QCOMPARE(again.server, QStringLiteral("10.0.67.131"));
        QCOMPARE(again.generalNotes, QStringLiteral("Sin observaciones"));
        QVERIFY(!again.characteristics[0].satisfied);
        QCOMPARE(f.records.cyclesFor(t.issueId).size(), 2);
    }

    void theSecondRevisionInheritsTheEnvironmentOfTheFirst() {
        AppFixture f;
        const Testing t = issueInTesting(f);
        addCycle(f, t.planId, QStringLiteral("Plan GREQ 2026997"), QDateTime::currentDateTime(),
                 {{QStringLiteral("TC-101"), Verdict::Fallido}});
        QualityRecord first = f.records.draftFor(t.issueId);
        first.server = QStringLiteral("10.0.67.131");
        first.moduleLink = QStringLiteral("https://gitlab.test/proyecto");
        QVERIFY(f.records.generate(t.issueId, first, QStringLiteral("/tmp/acta1.docx")).ok);
        f.issues.closeRevision(t.issueId, QaOutcome::Observado);

        f.issues.notePlanStarted(t.planId);   // vuelve a pruebas: revisión 2
        const QualityRecord second = f.records.draftFor(t.issueId);
        QCOMPARE(second.revisionNumber, 2);
        QCOMPARE(second.server, QStringLiteral("10.0.67.131"));
        QCOMPARE(second.moduleLink, QStringLiteral("https://gitlab.test/proyecto"));
    }

    void aRecordThatCouldNotBeWrittenChangesNothing() {
        AppFixture f;
        const Testing t = issueInTesting(f);
        f.recordWriter->fail = true;
        const auto result = f.records.generate(t.issueId, f.records.draftFor(t.issueId), QStringLiteral("/tmp/acta.docx"));
        QVERIFY(!result.ok);
        QCOMPARE(result.error, QStringLiteral("no se pudo escribir"));
        QVERIFY(!f.issues.find(t.issueId)->currentRevision()->hasDocument());
    }

    void theSuggestedNameAndTheSummaryUseTheRequirement() {
        AppFixture f;
        const Testing t = issueInTesting(f);
        addCycle(f, t.planId, QStringLiteral("Plan GREQ 2026997"), QDateTime::currentDateTime(),
                 {{QStringLiteral("TC-101"), Verdict::Superado}});
        const QString name = f.records.suggestedFileName(t.issueId);
        QVERIFY(name.startsWith(QStringLiteral("ControlCalidad_2026997_")));
        QVERIFY(name.endsWith(QStringLiteral(".docx")));

        const QString summary = f.records.summaryFor(t.issueId, f.records.draftFor(t.issueId), QaOutcome::Conforme);
        QVERIFY(summary.contains(QStringLiteral("GREQ 2026997 — revisión 1: Conforme")));
        QVERIFY(summary.contains(QStringLiteral("Casos ejecutados: 1 de 1")));
        QVERIFY(summary.contains(QStringLiteral("Plan «Plan GREQ 2026997»")));
        QVERIFY(summary.contains(QStringLiteral("Sin observaciones")));
    }
};

QTEST_MAIN(QualityRecordServiceTest)
#include "test_quality_record_service.moc"
