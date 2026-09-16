// QualityRecord y QualityRecordDraft (core/models/): el acta de control de calidad como datos —
// clasificaciones, totales y fechas— y el borrador que QAflow propone a partir del requerimiento, de
// los ciclos de plan que se ejecutaron y de los bugs.

#include "core/models/QualityRecord.h"
#include "core/models/QualityRecordDraft.h"

#include <QtTest>

using namespace qaflow;

namespace {

Issue importedIssue() {
    Issue issue;
    issue.id = QStringLiteral("IS-0001");
    issue.title = QStringLiteral("Integración de nuevos servicios");
    issue.planIds = {QStringLiteral("PL-1")};
    issue.requirement.connection = QStringLiteral("http://gesreq.test/greq");
    issue.requirement.data.id = QStringLiteral("2026997");
    issue.requirement.data.system = QStringLiteral("SUMA2-INGRESO");
    issue.requirement.data.systemCode = QStringLiteral("SUMA2");
    issue.requirement.data.summary = QStringLiteral("Servicios backend");
    issue.requirement.detail.id = QStringLiteral("2026997");
    issue.requirement.detail.description = QStringLiteral("Alcance:\n• Nuevos servicios");
    issue.requirement.detail.fields = {RequirementField{QStringLiteral("Desarrollado por"), QStringLiteral("CANAZA ESTEBAN")}};
    issue.publication.key = QStringLiteral("SUMA2-2907");
    issue.publication.url = QStringLiteral("http://jira.test/browse/SUMA2-2907");
    issue.publication.publishedTitle = QStringLiteral("QA - Elaboración de casos de prueba GREQ 2026997");
    return issue;
}

QList<TestCase> twoCases() {
    TestCase a;
    a.id = QStringLiteral("TC-1");
    a.title = QStringLiteral("Alta de DAV");
    a.steps = {TestStep{QStringLiteral("abrir"), QStringLiteral("se abre")}};
    TestCase b;
    b.id = QStringLiteral("TC-2");
    b.title = QStringLiteral("Atraque");
    b.jiraKey = QStringLiteral("SUMA2-2909");
    b.steps = {TestStep{QStringLiteral("a"), QStringLiteral("b")}, TestStep{QStringLiteral("c"), QStringLiteral("d")}};
    return {a, b};
}

RunRecord runOf(const QString& id, const QString& caseId, Verdict verdict, const QDate& day, const QString& testKey = {}) {
    RunRecord r;
    r.id = id;
    r.caseId = caseId;
    r.caseTitle = caseId == QStringLiteral("TC-1") ? QStringLiteral("Alta de DAV") : QStringLiteral("Atraque");
    r.planRunId = QStringLiteral("PR-1");
    r.verdict = verdict;
    r.testKey = testKey;
    r.startedAt = QDateTime(day, QTime(9, 0));
    r.finishedAt = QDateTime(day, QTime(9, 30));
    return r;
}

/// Un ciclo del plan del issue con sus ejecuciones, que es de lo que habla el acta.
PlanReport cycleOf(const QList<RunRecord>& runs, const QDate& from, const QDate& to,
                   const QString& zephyrCycleId = QString()) {
    PlanRun plan;
    plan.id = QStringLiteral("PR-1");
    plan.planId = QStringLiteral("PL-1");
    plan.name = QStringLiteral("Plan GREQ 2026997");
    plan.caseIds = {QStringLiteral("TC-1"), QStringLiteral("TC-2")};
    plan.startedAt = QDateTime(from, QTime(9, 0));
    plan.finishedAt = QDateTime(to, QTime(18, 0));
    plan.zephyrCycleId = zephyrCycleId;
    const QList<TestCase> cases = twoCases();
    return PlanReport::build(plan, runs, [&cases](const QString& caseId) {
        for (const auto& c : cases)
            if (c.id == caseId) return PlanReport::CaseInfo{c.title, c.jiraKey};
        return PlanReport::CaseInfo{};
    });
}

IssueLink bugOf(const QString& key, const QString& caseId, const QString& classification, bool resolved = false) {
    IssueLink bug;
    bug.key = key;
    bug.url = QStringLiteral("http://jira.test/browse/%1").arg(key);
    bug.title = QStringLiteral("Falla en %1").arg(caseId);
    bug.caseId = caseId;
    bug.classification = classification;
    bug.resolved = resolved;
    return bug;
}

} // namespace

class QualityRecordTest : public QObject {
    Q_OBJECT
private slots:
    // ---- El acta como datos ---------------------------------------------------------------------
    void theSummaryHasTheFiveTypesAndItsTotals() {
        QualityRecord r;
        QCOMPARE(r.observations.size(), 5);
        QCOMPARE(r.observations[0].type, QStringLiteral("A"));
        QCOMPARE(r.observations[4].type, QStringLiteral("E"));
        QCOMPARE(r.totalObservations(), 0);
        r.observations[0].observations = 3;
        r.observations[2].observations = 1;
        r.observations[1].corrections = 2;
        QCOMPARE(r.totalObservations(), 4);
        QCOMPARE(r.totalCorrections(), 2);
    }

    void theFormComesWithItsFourCharacteristicsAndItsDefaults() {
        const QualityRecord r;
        QCOMPARE(r.characteristics.size(), 4);
        QVERIFY(r.characteristics[0].text.startsWith(QStringLiteral("Existen las ayudas")));
        QVERIFY(r.characteristics[0].satisfied);
        QCOMPARE(r.server, QStringLiteral("S/D"));
        QCOMPARE(r.tables, QStringLiteral("n/a"));
        QCOMPARE(r.process, QStringLiteral("Según Requerimiento"));
        QVERIFY(r.isEmpty());
    }

    void theReviewDatesAreWrittenAsTheFormDoes() {
        QualityRecord r;
        QVERIFY(r.reviewDates().isEmpty());
        r.from = QDate(2026, 9, 9);
        QCOMPARE(r.reviewDates(), QStringLiteral("09/09/2026"));
        r.to = QDate(2026, 9, 9);
        QCOMPARE(r.reviewDates(), QStringLiteral("09/09/2026"));   // un solo día no se repite
        r.to = QDate(2026, 9, 10);
        QCOMPARE(r.reviewDates(), QStringLiteral("09/09/2026 a 10/09/2026"));
    }

    // ---- El borrador ----------------------------------------------------------------------------
    void theDraftFillsWhatTheProjectAlreadyKnows() {
        const Issue issue = importedIssue();
        const QList<RunRecord> runs{runOf(QStringLiteral("R-1"), QStringLiteral("TC-1"), Verdict::Fallido, QDate(2026, 9, 9),
                                          QStringLiteral("SUMA2-2908")),
                                    runOf(QStringLiteral("R-2"), QStringLiteral("TC-2"), Verdict::Superado, QDate(2026, 9, 10))};
        const QList<IssueLink> bugs{bugOf(QStringLiteral("SUMA2-2912"), QStringLiteral("TC-1"), QStringLiteral("A")),
                                    bugOf(QStringLiteral("SUMA2-2915"), QStringLiteral("TC-1"), QStringLiteral("C"))};
        quality::DraftContext context;
        context.qaResource = QStringLiteral("MAIDANA JUAN JONAS");
        context.revisionNumber = 2;
        const PlanReport cycle = cycleOf(runs, QDate(2026, 9, 9), QDate(2026, 9, 10));

        const QualityRecord r = quality::draftFor(issue, twoCases(), {cycle}, bugs, context);
        QCOMPARE(r.greq, QStringLiteral("2026997"));
        QCOMPARE(r.system, QStringLiteral("SUMA2-INGRESO"));
        QCOMPARE(r.description, QStringLiteral("Alcance:\n• Nuevos servicios"));
        QCOMPARE(r.developedBy, QStringLiteral("CANAZA ESTEBAN"));
        QCOMPARE(r.qaResource, QStringLiteral("MAIDANA JUAN JONAS"));
        QCOMPARE(r.revisionNumber, 2);
        QCOMPARE(r.from, QDate(2026, 9, 9));
        QCOMPARE(r.to, QDate(2026, 9, 10));

        // Observaciones por tipo
        QCOMPARE(r.observations[0].observations, 1);   // A
        QCOMPARE(r.observations[2].observations, 1);   // C
        QCOMPARE(r.totalObservations(), 2);
        QCOMPARE(r.totalCorrections(), 0);

        // Detalles: el issue publicado, los casos con su Test y los bugs
        QVERIFY(r.caseDesign.contains(QStringLiteral("QA - Elaboración de casos de prueba GREQ 2026997")));
        QVERIFY(r.caseDesign.contains(QStringLiteral("http://jira.test/browse/SUMA2-2907")));
        QVERIFY(r.caseDesign.contains(QStringLiteral("SUMA2-2908 — Alta de DAV — 1 paso(s)")));
        QVERIFY(r.caseDesign.contains(QStringLiteral("SUMA2-2909 — Atraque — 2 paso(s)")));   // sin Test, su historia
        QVERIFY(r.caseDesign.contains(QStringLiteral("Plan «Plan GREQ 2026997» · 2 caso(s)")));
        QVERIFY(r.execution.contains(QStringLiteral("2 de 2 ejecutados: 1 superado(s), 1 fallido(s), 0 bloqueado(s)")));
        QVERIFY(r.execution.contains(QStringLiteral("TC-1 Alta de DAV — Fallido")));
        QVERIFY(r.bugs.contains(QStringLiteral("http://jira.test/browse/SUMA2-2912 — Falla en TC-1")));
        QVERIFY(r.bugs.contains(QStringLiteral("SUMA2-2915")));
    }

    void theDraftInheritsTheEnvironmentFromThePreviousRecordAndCountsCorrections() {
        QualityRecord previous;
        previous.greq = QStringLiteral("2026000");
        previous.server = QStringLiteral("10.0.67.131");
        previous.dbSchema = QStringLiteral("SUMA2");
        previous.department = QStringLiteral("Departamento de Pruebas");
        previous.moduleLink = QStringLiteral("https://gitlab.test/proyecto");
        previous.logoPath = QStringLiteral("/tmp/logo.png");

        quality::DraftContext context;
        context.previous = previous;
        context.revisionNumber = 2;
        context.previousBugs = {bugOf(QStringLiteral("SUMA2-1"), QStringLiteral("TC-1"), QStringLiteral("A"), true),
                                bugOf(QStringLiteral("SUMA2-2"), QStringLiteral("TC-1"), QStringLiteral("A"), false),
                                bugOf(QStringLiteral("SUMA2-3"), QStringLiteral("TC-2"), QStringLiteral("B"), true)};

        const QualityRecord r = quality::draftFor(importedIssue(), twoCases(), {}, {}, context);
        QCOMPARE(r.server, QStringLiteral("10.0.67.131"));
        QCOMPARE(r.dbSchema, QStringLiteral("SUMA2"));
        QCOMPARE(r.department, QStringLiteral("Departamento de Pruebas"));
        QCOMPARE(r.moduleLink, QStringLiteral("https://gitlab.test/proyecto"));
        QCOMPARE(r.logoPath, QStringLiteral("/tmp/logo.png"));
        QCOMPARE(r.greq, QStringLiteral("2026997"));      // el requerimiento es el de ahora, no el anterior
        QCOMPARE(r.dbUser, QStringLiteral("S/D"));        // lo que nadie rellenó sigue como en el formulario
        // Correcciones: las observaciones de la ronda anterior que ya están cerradas.
        QCOMPARE(r.observations[0].corrections, 1);
        QCOMPARE(r.observations[1].corrections, 1);
        QCOMPARE(r.totalCorrections(), 2);
        QCOMPARE(r.totalObservations(), 0);
    }

    void theSummarySaysHowTheRevisionWent() {
        QualityRecord r;
        r.greq = QStringLiteral("2026997");
        r.revisionNumber = 2;
        r.from = QDate(2026, 9, 9);
        r.to = QDate(2026, 9, 10);
        r.observations[0].observations = 2;
        r.generalNotes = QStringLiteral("Se reprograma");
        const QList<RunRecord> runs{runOf(QStringLiteral("R-1"), QStringLiteral("TC-1"), Verdict::Fallido, QDate(2026, 9, 9)),
                                    runOf(QStringLiteral("R-2"), QStringLiteral("TC-1"), Verdict::Superado, QDate(2026, 9, 10)),
                                    runOf(QStringLiteral("R-3"), QStringLiteral("TC-2"), Verdict::Bloqueado, QDate(2026, 9, 10))};

        const PlanReport cycle = cycleOf(runs, QDate(2026, 9, 9), QDate(2026, 9, 10));
        const QString summary = quality::summaryOf(r, QaOutcome::Observado, {cycle});
        QVERIFY(summary.contains(QStringLiteral("GREQ 2026997 — revisión 2: Observado")));
        QVERIFY(summary.contains(QStringLiteral("09/09/2026 a 10/09/2026")));
        // TC-1 se repitió: cuenta su última ejecución (superada), como en el acta.
        QVERIFY(summary.contains(QStringLiteral("Casos ejecutados: 2 de 2 (1 superados, 0 fallidos, 1 bloqueados)")));
        QVERIFY(summary.contains(QStringLiteral("A · Funcionamiento/Lógica: 2")));
        QVERIFY(summary.contains(QStringLiteral("Se reprograma")));

        r.observations[0].observations = 0;
        QVERIFY(quality::summaryOf(r, QaOutcome::Conforme, {cycle}).contains(QStringLiteral("Sin observaciones")));
    }
};

QTEST_APPLESS_MAIN(QualityRecordTest)
#include "test_quality_record.moc"
