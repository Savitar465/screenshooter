// QualityRecordService (application/QualityRecordService.h): el acta que se propone para la revisión
// en curso (sólo con lo ejecutado en ella), lo que se hereda del acta anterior, lo que corrigió el
// usuario, la generación del fichero y el resumen que se manda a Jira y a GESREQ.

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

/// Un issue importado con dos casos de ejemplo y su plan, ya en pruebas (revisión 1 abierta).
QString issueInTesting(AppFixture& f) {
    f.issues.importRequirements({requirement()}, kConnection);
    const QString id = f.issues.issues().first().id;
    f.issues.linkCase(id, QStringLiteral("TC-101"));
    f.issues.linkCase(id, QStringLiteral("TC-102"));
    f.issues.linkPlan(id, QStringLiteral("PL-0001"));
    f.issues.notePlanStarted(QStringLiteral("PL-0001"));
    return id;
}

/// Archiva una ejecución del caso con ese veredicto (el historial le pone el id).
void archiveRun(AppFixture& f, const QString& caseId, Verdict verdict, const QDateTime& when) {
    RunRecord run;
    run.caseId = caseId;
    run.caseTitle = QStringLiteral("Caso %1").arg(caseId);
    run.verdict = verdict;
    run.startedAt = when;
    run.finishedAt = when.addSecs(600);
    f.history.addRun(run);
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
        const QDateTime beforeEverything = QDateTime::currentDateTime().addDays(-10);
        archiveRun(f, QStringLiteral("TC-101"), Verdict::Fallido, beforeEverything);   // de antes de la revisión
        const QString id = issueInTesting(f);
        const QDateTime now = QDateTime::currentDateTime();
        archiveRun(f, QStringLiteral("TC-101"), Verdict::Superado, now);
        archiveRun(f, QStringLiteral("TC-102"), Verdict::Fallido, now);

        const QualityRecord draft = f.records.draftFor(id);
        QCOMPARE(draft.greq, QStringLiteral("2026997"));
        QCOMPARE(draft.system, QStringLiteral("SUMA2-INGRESO"));
        QCOMPARE(draft.revisionNumber, 1);
        QVERIFY(draft.execution.contains(QStringLiteral("2 caso(s) ejecutado(s): 1 superado(s), 1 fallido(s)")));
        QCOMPARE(f.records.revisionRuns(*f.issues.find(id)).size(), 2);   // la ejecución vieja no entra

        const IssueProgress progress = f.records.progressFor(id);
        QCOMPARE(progress.cases, 2);
        QCOMPARE(progress.executed, 2);
        QCOMPARE(progress.failed, 1);
        QVERIFY(progress.suggested == QaOutcome::Observado);
        QVERIFY(progress.canClose());
    }

    void bugsOfTheRevisionAreObservationsAndTheOlderFixedOnesAreCorrections() {
        AppFixture f;
        const QDateTime old = QDateTime::currentDateTime().addDays(-5);
        f.bugLedger.recordIssue(bugOf(QStringLiteral("SHOP-1"), QStringLiteral("TC-101"), QStringLiteral("A"), old, true));
        f.bugLedger.recordIssue(bugOf(QStringLiteral("SHOP-2"), QStringLiteral("TC-101"), QStringLiteral("B"), old, false));
        const QString id = issueInTesting(f);
        const QDateTime now = QDateTime::currentDateTime();
        archiveRun(f, QStringLiteral("TC-101"), Verdict::Fallido, now);
        f.bugLedger.recordIssue(bugOf(QStringLiteral("SHOP-3"), QStringLiteral("TC-101"), QStringLiteral("A"), now));
        f.bugLedger.recordIssue(bugOf(QStringLiteral("SHOP-4"), QStringLiteral("TC-102"), QStringLiteral("C"), now));

        const QualityRecord draft = f.records.draftFor(id);
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
        const QString id = issueInTesting(f);
        archiveRun(f, QStringLiteral("TC-101"), Verdict::Superado, QDateTime::currentDateTime());

        QualityRecord record = f.records.draftFor(id);
        record.server = QStringLiteral("10.0.67.131");
        record.dbSchema = QStringLiteral("SUMA2");
        record.generalNotes = QStringLiteral("Sin observaciones");
        record.characteristics[0].satisfied = false;
        const QString path = QStringLiteral("/tmp/ControlCalidad_2026997.docx");
        const auto result = f.records.generate(id, record, path);
        QVERIFY(result.ok);
        QCOMPARE(result.path, path);
        QCOMPARE(f.recordWriter->calls, 1);
        QCOMPARE(f.recordWriter->lastRecord.server, QStringLiteral("10.0.67.131"));

        const IssueRevision* revision = f.issues.find(id)->currentRevision();
        QVERIFY(revision);
        QCOMPARE(revision->documentPath, path);
        QCOMPARE(revision->record.dbSchema, QStringLiteral("SUMA2"));

        // Volver a pedir el borrador conserva lo corregido y actualiza lo que sale del proyecto.
        archiveRun(f, QStringLiteral("TC-102"), Verdict::Fallido, QDateTime::currentDateTime());
        const QualityRecord again = f.records.draftFor(id);
        QCOMPARE(again.server, QStringLiteral("10.0.67.131"));
        QCOMPARE(again.generalNotes, QStringLiteral("Sin observaciones"));
        QVERIFY(!again.characteristics[0].satisfied);
        QVERIFY(again.execution.contains(QStringLiteral("2 caso(s) ejecutado(s)")));
    }

    void theSecondRevisionInheritsTheEnvironmentOfTheFirst() {
        AppFixture f;
        const QString id = issueInTesting(f);
        archiveRun(f, QStringLiteral("TC-101"), Verdict::Fallido, QDateTime::currentDateTime());
        QualityRecord first = f.records.draftFor(id);
        first.server = QStringLiteral("10.0.67.131");
        first.moduleLink = QStringLiteral("https://gitlab.test/proyecto");
        QVERIFY(f.records.generate(id, first, QStringLiteral("/tmp/acta1.docx")).ok);
        f.issues.closeRevision(id, QaOutcome::Observado);

        f.issues.notePlanStarted(QStringLiteral("PL-0001"));   // vuelve a pruebas: revisión 2
        const QualityRecord second = f.records.draftFor(id);
        QCOMPARE(second.revisionNumber, 2);
        QCOMPARE(second.server, QStringLiteral("10.0.67.131"));
        QCOMPARE(second.moduleLink, QStringLiteral("https://gitlab.test/proyecto"));
    }

    void aRecordThatCouldNotBeWrittenChangesNothing() {
        AppFixture f;
        const QString id = issueInTesting(f);
        f.recordWriter->fail = true;
        const auto result = f.records.generate(id, f.records.draftFor(id), QStringLiteral("/tmp/acta.docx"));
        QVERIFY(!result.ok);
        QCOMPARE(result.error, QStringLiteral("no se pudo escribir"));
        QVERIFY(!f.issues.find(id)->currentRevision()->hasDocument());
    }

    void theSuggestedNameAndTheSummaryUseTheRequirement() {
        AppFixture f;
        const QString id = issueInTesting(f);
        archiveRun(f, QStringLiteral("TC-101"), Verdict::Superado, QDateTime::currentDateTime());
        const QString name = f.records.suggestedFileName(id);
        QVERIFY(name.startsWith(QStringLiteral("ControlCalidad_2026997_")));
        QVERIFY(name.endsWith(QStringLiteral(".docx")));

        const QString summary = f.records.summaryFor(id, f.records.draftFor(id), QaOutcome::Conforme);
        QVERIFY(summary.contains(QStringLiteral("GREQ 2026997 — revisión 1: Conforme")));
        QVERIFY(summary.contains(QStringLiteral("Casos ejecutados: 1")));
        QVERIFY(summary.contains(QStringLiteral("Sin observaciones")));
    }
};

QTEST_MAIN(QualityRecordServiceTest)
#include "test_quality_record_service.moc"
