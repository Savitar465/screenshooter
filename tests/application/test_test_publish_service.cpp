// TestPublishService (application/): traduce el informe de un ciclo de plan a lo que espera la
// herramienta de gestión de pruebas. Cubre qué casos entran, de dónde sale el Test de cada ejecución
// —el que ya tiene de una publicación anterior o el que se crea a partir del caso— y cómo viajan
// las evidencias con su paso.

#include "support/AppFixture.h"
#include "support/FakeTestManagement.h"

#include "application/TestPublishService.h"

#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;
using qaflow::testing::FakeTestManagement;

namespace {
/// Informe con dos casos ejecutados (uno fallido) y uno pendiente.
PlanReport reportWith(const QList<QPair<QString, Verdict>>& executed, const QString& pendingCase = QString()) {
    PlanReport report;
    report.plan.id = QStringLiteral("PR-0001");
    report.plan.name = QStringLiteral("Regresión Sprint 14");
    report.plan.startedAt = QDateTime(QDate(2026, 5, 12), QTime(9, 0));
    report.plan.finishedAt = QDateTime(QDate(2026, 5, 12), QTime(11, 30));
    for (const auto& [caseId, verdict] : executed) {
        PlanReportRow row;
        row.caseId = caseId;
        row.title = QStringLiteral("Caso %1").arg(caseId);
        row.executed = true;
        row.run.id = QStringLiteral("R-%1").arg(caseId.right(1));   // la ejecución de ese caso
        row.run.caseId = caseId;
        row.run.verdict = verdict;
        row.run.durationSecs = 245;
        row.run.steps = {RunRecordStep{QStringLiteral("Abrir carrito"), {}, QStringLiteral("Se abre"), StepResult::Pass, {}, 30},
                         RunRecordStep{QStringLiteral("Aplicar cupón"), QStringLiteral("Cupón QA10"), QStringLiteral("Descuenta"),
                                       verdict == Verdict::Fallido ? StepResult::Fail : StepResult::Pass,
                                       QStringLiteral("El total no cambia"), 45}};
        report.rows << row;
        ++report.executed;
        if (verdict == Verdict::Superado) ++report.passed; else ++report.failed;
    }
    if (!pendingCase.isEmpty()) {
        PlanReportRow row;
        row.caseId = pendingCase;
        row.title = QStringLiteral("Sin ejecutar");
        report.rows << row;
    }
    return report;
}

/// El issue de un requerimiento importado, para los ciclos que lo prueban.
QString issueFor(AppFixture& f) {
    ExternalRequirement requirement;
    requirement.id = QStringLiteral("2026997");
    requirement.summary = QStringLiteral("Cupones de descuento");
    return f.issues.openForRequirement(requirement, QStringLiteral("http://servidor:7401/greq"));
}

PlanReport issueReport(const QString& issueId, const QString& id, const QString& phase, const QList<QPair<QString, Verdict>>& executed) {
    PlanReport report = reportWith(executed);
    report.plan.id = id;
    report.plan.issueId = issueId;
    report.plan.revision = 1;
    report.plan.environment = phase;
    return report;
}
} // namespace

class TestPublishServiceTest : public QObject {
    Q_OBJECT
private slots:
    // Un requerimiento tiene en Zephyr un ciclo por fase y un Test por caso: los ciclos de plan de la
    // misma fase van al mismo ciclo, y el Test del caso es el mismo en QA y en PRE.
    void anIssueHasOneCyclePerPhaseAndOneTestPerCase() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);
        publish.setIssues(&f.issues);
        const QString issueId = issueFor(f);

        // Primer ciclo de QA: crea el ciclo de la fase y el Test del caso.
        zephyr->resultToReturn.createdTests.insert(QStringLiteral("TC-101"), QStringLiteral("SHOP-77"));
        PublishResult out;
        publish.publish(issueReport(issueId, QStringLiteral("PR-1"), QStringLiteral("QA"), {{QStringLiteral("TC-101"), Verdict::Fallido}}),
                        [&out](const PublishResult& r) { out = r; });
        QVERIFY(out.ok);
        QVERIFY(zephyr->published[0].cycleId.isEmpty());
        QCOMPARE(zephyr->published[0].cycleName, QStringLiteral("GREQ 2026997 · QA"));
        QVERIFY(zephyr->published[0].testContext.contains(QStringLiteral("GREQ 2026997")));
        const Issue* issue = f.issues.find(issueId);
        QCOMPARE(issue->zephyr.tests.value(QStringLiteral("TC-101")), QStringLiteral("SHOP-77"));
        QCOMPARE(issue->zephyr.cycleOf(QStringLiteral("QA")), QStringLiteral("77"));
        QCOMPARE(issue->zephyr.cycleNameOf(QStringLiteral("QA")), QStringLiteral("GREQ 2026997 · QA"));

        // El reintento en QA (otro ciclo de plan) actualiza ese ciclo con el mismo Test.
        zephyr->resultToReturn = PublishResult{};
        PlanReport retest = issueReport(issueId, QStringLiteral("PR-2"), QStringLiteral("QA"), {{QStringLiteral("TC-101"), Verdict::Superado}});
        QVERIFY(publish.casesNeedingTest(retest).isEmpty());
        publish.publish(retest, [&out](const PublishResult& r) { out = r; });
        QCOMPARE(zephyr->published[1].cycleId, QStringLiteral("77"));
        QCOMPARE(zephyr->published[1].cases.first().testKey, QStringLiteral("SHOP-77"));

        // PRE: otro ciclo de Zephyr, el mismo Test.
        zephyr->nextCycleId = QStringLiteral("88");
        publish.publish(issueReport(issueId, QStringLiteral("PR-3"), QStringLiteral("PRE"), {{QStringLiteral("TC-101"), Verdict::Superado}}),
                        [&out](const PublishResult& r) { out = r; });
        QVERIFY(zephyr->published[2].cycleId.isEmpty());
        QCOMPARE(zephyr->published[2].cycleName, QStringLiteral("GREQ 2026997 · PRE"));
        QCOMPARE(zephyr->published[2].cases.first().testKey, QStringLiteral("SHOP-77"));
        QCOMPARE(f.issues.find(issueId)->zephyr.cycleOf(QStringLiteral("PRE")), QStringLiteral("88"));
    }

    // Si el ciclo de la fase se borró en Zephyr, se olvida y se publica en uno nuevo.
    void aDeletedPhaseCycleIsCreatedAgain() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);
        publish.setIssues(&f.issues);
        const QString issueId = issueFor(f);
        f.issues.noteZephyrCycle(issueId, QStringLiteral("QA"), QStringLiteral("70"), QStringLiteral("GREQ 2026997 · QA"));
        zephyr->missingCycles = {QStringLiteral("70")};

        PublishResult out;
        publish.publish(issueReport(issueId, QStringLiteral("PR-1"), QStringLiteral("QA"), {{QStringLiteral("TC-101"), Verdict::Superado}}),
                        [&out](const PublishResult& r) { out = r; });
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(zephyr->published.size(), 2);
        QCOMPARE(zephyr->published[0].cycleId, QStringLiteral("70"));
        QVERIFY(zephyr->published[1].cycleId.isEmpty());
        QCOMPARE(f.issues.find(issueId)->zephyr.cycleOf(QStringLiteral("QA")), QStringLiteral("77"));
    }

    // Los Tests del requerimiento se pueden crear antes de probarlo; los que ya tiene no se repiten.
    void theTestsOfAnIssueAreCreatedBeforeTesting() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);
        publish.setIssues(&f.issues);
        const QString issueId = issueFor(f);
        const QStringList cases{QStringLiteral("TC-101"), QStringLiteral("TC-102")};
        QCOMPARE(publish.casesWithoutTest(issueId, cases), cases);

        PublishResult out;
        publish.createTests(issueId, cases, [&out](const PublishResult& r) { out = r; });
        QVERIFY(out.ok);
        QCOMPARE(zephyr->testsRequested.size(), 1);
        QCOMPARE(zephyr->testsRequested[0].cases.size(), 2);
        QVERIFY(!zephyr->testsRequested[0].cases[0].design.isEmpty());   // con los pasos del caso
        QCOMPARE(f.issues.find(issueId)->zephyr.tests.value(QStringLiteral("TC-101")), QStringLiteral("SHOP-101"));
        QVERIFY(publish.casesWithoutTest(issueId, cases).isEmpty());

        publish.createTests(issueId, cases, [&out](const PublishResult& r) { out = r; });
        QVERIFY(out.ok);
        QCOMPARE(zephyr->testsRequested.size(), 1);   // nada que crear: no se llama a Zephyr
    }

    void publishingIsOffUntilZephyrIsEnabledForJira() {
        AppFixture f;
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);
        QVERIFY(!publish.enabled());                       // Zephyr desactivado en los ajustes

        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        QVERIFY(publish.enabled());

        f.settings.updateTracker([](TrackerSettings& s) { s.kind = TrackerKind::GitHub; });
        QVERIFY(!publish.enabled());                       // Zephyr es un plugin de Jira

        PublishResult out;
        publish.publish(reportWith({{QStringLiteral("TC-101"), Verdict::Superado}}), [&](const PublishResult& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(zephyr->published.isEmpty());
    }

    void onlyExecutedCasesTravelAndTheyCarryTheTestOfTheirRun() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; s.zephyrVersion = QStringLiteral("2.3.0"); });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);

        PlanReport report = reportWith({{QStringLiteral("TC-101"), Verdict::Superado},
                                        {QStringLiteral("TC-102"), Verdict::Fallido}},
                                       QStringLiteral("TC-103"));
        report.rows[0].run.testKey = QStringLiteral("SHOP-42");   // ya publicada: tiene su Test
        report.rows[1].run.testKey = QStringLiteral("SHOP-43");
        PublishResult out;
        publish.publish(report, [&](const PublishResult& r) { out = r; });
        QVERIFY(out.ok);
        QCOMPARE(zephyr->published.size(), 1);
        const PublishRequest& req = zephyr->published[0];
        QCOMPARE(req.cases.size(), 2);                     // el pendiente no se publica
        QCOMPARE(req.cases[0].runId, QStringLiteral("R-1"));
        QCOMPARE(req.cases[0].testKey, QStringLiteral("SHOP-42"));
        QCOMPARE(req.cases[1].testKey, QStringLiteral("SHOP-43"));
        QCOMPARE(static_cast<int>(req.cases[1].verdict), static_cast<int>(Verdict::Fallido));
        QCOMPARE(req.cases[1].steps.size(), 2);
        QCOMPARE(req.versionName, QStringLiteral("2.3.0"));
        QVERIFY(req.cycleName.startsWith(QStringLiteral("Regresión Sprint 14")));
        QVERIFY(req.cycleName.contains(QStringLiteral("12/05/2026")));   // el ciclo lleva la fecha
        QCOMPARE(req.startedAt, report.plan.startedAt);
    }

    // Un ciclo que prueba un requerimiento va al ciclo de Zephyr de su fase, que se llama por el
    // requerimiento y la fase; la ronda y el ambiente siguen en su descripción.
    void theCycleCarriesTheRequirementTheRevisionAndTheEnvironment() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);
        publish.setIssues(&f.issues);

        ExternalRequirement requirement;
        requirement.id = QStringLiteral("2026997");
        requirement.summary = QStringLiteral("Cupones de descuento");
        const QString issueId = f.issues.openForRequirement(requirement, QStringLiteral("http://servidor:7401/greq"));
        QVERIFY(!issueId.isEmpty());

        PlanReport report = reportWith({{QStringLiteral("TC-101"), Verdict::Superado}});
        report.plan.issueId = issueId;
        report.plan.revision = 2;
        report.plan.environment = QStringLiteral("QA");
        QCOMPARE(publish.cycleName(report), QStringLiteral("GREQ 2026997 · QA"));

        publish.publish(report, [](const PublishResult&) {});
        const PublishRequest& sent = zephyr->published[0];
        QCOMPARE(sent.environment, QStringLiteral("QA"));   // y en el campo «environment» del ciclo
        QVERIFY(sent.description.contains(QStringLiteral("GREQ 2026997")));
        QVERIFY(sent.description.contains(QStringLiteral("Revisión 2")));
        QVERIFY(sent.description.contains(QStringLiteral("Ambiente: QA")));

        // Un ciclo suelto se queda con el nombre de siempre: no se inventa lo que no hay.
        PlanReport loose = reportWith({{QStringLiteral("TC-101"), Verdict::Superado}});
        QCOMPARE(publish.cycleName(loose), QStringLiteral("Regresión Sprint 14 · 12/05/2026"));

        // Y una continuación de la misma fase va al mismo ciclo: actualiza sus ejecuciones.
        const QString cycleId = f.history.startPlan(QStringLiteral("Regresión Sprint 14"), {QStringLiteral("TC-101")},
                                                    QStringLiteral("PL-0001"), QStringLiteral("QA"));
        PlanReport continued = reportWith({{QStringLiteral("TC-101"), Verdict::Superado}});
        continued.plan.issueId = issueId;
        continued.plan.revision = 2;
        continued.plan.environment = QStringLiteral("QA");
        continued.plan.continuesCycleId = cycleId;
        QCOMPARE(publish.cycleName(continued), QStringLiteral("GREQ 2026997 · QA"));
        // Un ciclo suelto que continúa a otro sí lleva por dónde va la cadena: su ciclo es propio.
        continued.plan.issueId.clear();
        QCOMPARE(publish.cycleName(continued), QStringLiteral("Rev. 2 · Regresión Sprint 14 · Cont. 1 · 12/05/2026 · QA"));
    }

    // Zephyr rechaza con un 406 genérico un ciclo cuyo nombre o descripción pasan de 255
    // caracteres; con el título de un requerimiento real (168) la descripción se pasaba.
    void aLongRequirementTitleStillFitsInTheCycle() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);
        publish.setIssues(&f.issues);

        ExternalRequirement requirement;
        requirement.id = QStringLiteral("2025749");
        requirement.summary = QStringLiteral("Adecuar el sistema SA- GESTION ARCHIVO DESARCHIVO, con la adición de los reportes, "
                                             "con las funcioanlidades para los archivos Digitales, (puntos B Y C del GREQ 2025749).");
        const QString issueId = f.issues.openForRequirement(requirement, QStringLiteral("http://servidor:7401/greq"));
        QVERIFY(!issueId.isEmpty());

        PlanReport report = reportWith({{QStringLiteral("TC-101"), Verdict::Superado}});
        report.plan.name = QStringLiteral("IS-0001 · ") + requirement.summary + QStringLiteral(" ") + requirement.summary;
        report.plan.issueId = issueId;
        report.plan.revision = 1;
        report.plan.environment = QStringLiteral("QA");

        // El ciclo de la fase se llama por el requerimiento: el título largo no llega a su nombre.
        const QString name = publish.cycleName(report);
        QCOMPARE(name, QStringLiteral("GREQ 2025749 · QA"));
        // Uno suelto con ese plan sí lo lleva, y se acorta el plan, no lo demás.
        PlanReport loose = report;
        loose.plan.issueId.clear();
        const QString own = publish.cycleName(loose);
        QVERIFY(own.size() <= PublishRequest::kMaxCycleField);
        QVERIFY(own.startsWith(QStringLiteral("Rev. 1 · IS-0001 · Adecuar")));
        QVERIFY(own.endsWith(QStringLiteral("… · 12/05/2026 · QA")));

        publish.publish(report, [](const PublishResult&) {});
        const PublishRequest& sent = zephyr->published[0];
        QCOMPARE(sent.cycleName, name);
        QVERIFY2(sent.description.size() <= PublishRequest::kMaxCycleField, qPrintable(QString::number(sent.description.size())));
        QVERIFY(sent.description.contains(QStringLiteral("GREQ 2025749")));
        QVERIFY(sent.description.contains(QStringLiteral("Ambiente: QA")));
    }

    void casesWhoseTestWillBeCreatedAreListedBeforePublishing() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        TestPublishService publish(std::make_shared<FakeTestManagement>(), f.store, f.history, f.settings, f.bugLedger);
        PlanReport report = reportWith({{QStringLiteral("TC-101"), Verdict::Superado},
                                        {QStringLiteral("TC-102"), Verdict::Superado}});
        report.rows[0].run.testKey = QStringLiteral("SHOP-42");   // esta ejecución ya se publicó una vez
        QCOMPARE(publish.casesNeedingTest(report), QStringList{QStringLiteral("TC-102")});
    }

    // El caso sin Test no se queda fuera: viaja con lo que hace falta para crearlo.
    void casesWithoutTestKeyTravelWithTheirPreconditionsAndSteps() {
        AppFixture f;
        f.store.updateCase(QStringLiteral("TC-101"), [](TestCase& c) {
            c.title = QStringLiteral("Comprar con cupón");
            c.preconditions = QStringLiteral("Sesión iniciada con un usuario con carrito");
            c.steps = {TestStep{QStringLiteral("Abrir carrito"), {}, QStringLiteral("Se abre")},
                       TestStep{QStringLiteral("Aplicar cupón"), QStringLiteral("Cupón QA10"), QStringLiteral("Descuenta")},
                       TestStep{QStringLiteral("Pagar"), {}, QStringLiteral("Se confirma")}};
        });
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);

        publish.publish(reportWith({{QStringLiteral("TC-101"), Verdict::Superado}}), [](const PublishResult&) {});
        const PublishCase& sent = zephyr->published[0].cases[0];
        QVERIFY(sent.testKey.isEmpty());
        QCOMPARE(sent.preconditions, QStringLiteral("Sesión iniciada con un usuario con carrito"));
        // Los pasos del Test son los del caso, no sólo los que llegaron a ejecutarse.
        QCOMPARE(sent.design.size(), 3);
        QCOMPARE(sent.design[1].action, QStringLiteral("Aplicar cupón"));
        QCOMPARE(sent.design[1].expected, QStringLiteral("Descuenta"));
        QCOMPARE(sent.steps.size(), 2);
    }

    // La clave del Test creado se guarda en la ejecución publicada, no en el caso: republicar ese
    // informe reutiliza el Test, y otro ciclo del mismo caso estrena el suyo.
    void theKeyOfACreatedTestBelongsToTheRunNotToTheCase() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        zephyr->resultToReturn.ok = true;
        zephyr->resultToReturn.cycleId = QStringLiteral("77");
        zephyr->resultToReturn.testsCreated = 1;
        zephyr->resultToReturn.createdTests.insert(QStringLiteral("TC-103"), QStringLiteral("SHOP-77"));
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);

        // Dos ciclos del mismo caso, en el historial de verdad.
        f.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Ciclo 1"));
        const QString first = f.run.planRunId();
        f.run.mark(StepResult::Pass);
        f.run.finish();
        f.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Ciclo 2"));
        const QString second = f.run.planRunId();
        f.run.mark(StepResult::Pass);
        f.run.finish();

        publish.publish(f.history.report(first), [](const PublishResult&) {});
        QVERIFY(zephyr->published[0].cases[0].testKey.isEmpty());   // nunca publicada: se crea
        const RunRecord firstRun = f.history.runsForPlan(first).first();
        QCOMPARE(firstRun.testKey, QStringLiteral("SHOP-77"));
        QCOMPARE(f.history.report(first).rows[0].testKey, QStringLiteral("SHOP-77"));
        QVERIFY(f.history.findPlan(first)->isPublished());

        // El otro ciclo no hereda nada: su ejecución sigue sin Test y estrenará el suyo.
        QVERIFY(f.history.runsForPlan(second).first().testKey.isEmpty());
        QVERIFY(f.history.report(second).rows[0].testKey.isEmpty());
        QCOMPARE(publish.casesNeedingTest(f.history.report(second)), QStringList{QStringLiteral("TC-103")});
        publish.publish(f.history.report(second), [](const PublishResult&) {});
        QVERIFY(zephyr->published[1].cases[0].testKey.isEmpty());   // no reutiliza SHOP-77

        // Republicar el primer informe sí viaja con su Test en vez de pedir otro.
        publish.publish(f.history.report(first), [](const PublishResult&) {});
        QCOMPARE(zephyr->published[2].cases[0].testKey, QStringLiteral("SHOP-77"));
    }

    // Si el ciclo falla a medias, los Tests que ya se crearon se guardan igual: existen en Jira y el
    // reintento debe reutilizarlos, no duplicarlos.
    void createdTestsAreKeptEvenWhenTheCycleFails() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        zephyr->resultToReturn.ok = false;
        zephyr->resultToReturn.error = QStringLiteral("500");
        zephyr->resultToReturn.createdTests.insert(QStringLiteral("TC-103"), QStringLiteral("SHOP-78"));
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);
        f.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Ciclo"));
        const QString planRunId = f.run.planRunId();
        f.run.mark(StepResult::Pass);
        f.run.finish();

        publish.publish(f.history.report(planRunId), [](const PublishResult&) {});
        QVERIFY(!f.history.findPlan(planRunId)->isPublished());
        QCOMPARE(f.history.runsForPlan(planRunId).first().testKey, QStringLiteral("SHOP-78"));
    }

    // Actualizar manda los mismos resultados al ciclo de Zephyr en el que el informe ya está
    // publicado, en vez de crear otro; y sin publicar no hay nada que actualizar.
    void updatingTargetsTheCycleTheReportIsPublishedIn() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);

        PlanReport report = reportWith({{QStringLiteral("TC-101"), Verdict::Superado}});
        PublishResult out;
        publish.update(report, [&](const PublishResult& r) { out = r; });
        QVERIFY(!out.ok);                                  // no está publicado
        QVERIFY(zephyr->published.isEmpty());
        QVERIFY(publish.requestFor(report, true).cycleId.isEmpty());

        report.plan.zephyrCycleId = QStringLiteral("77");
        report.plan.publishedAt = QDateTime::currentDateTime();
        publish.update(report, [&](const PublishResult& r) { out = r; });
        QVERIFY(out.ok);
        QCOMPARE(zephyr->published.size(), 1);
        QCOMPARE(zephyr->published[0].cycleId, QStringLiteral("77"));
        QVERIFY(publish.requestFor(report).cycleId.isEmpty());   // publicar de nuevo sigue creando otro ciclo
    }

    // El enlace al ciclo en Jira sale del nombre con el que se publicó (plan y fecha) y de la URL del gestor.
    void cycleUrlPointsAtTheExecutionsOfThePublishedCycle() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; s.url = QStringLiteral("https://jira.acme.com"); s.project = QStringLiteral("SHOP"); });
        TestPublishService publish(std::make_shared<FakeTestManagement>(), f.store, f.history, f.settings, f.bugLedger);
        PlanReport report = reportWith({{QStringLiteral("TC-101"), Verdict::Superado}});
        QVERIFY(publish.cycleUrl(report).isEmpty());   // sin publicar no hay ciclo al que ir
        report.plan.zephyrCycleId = QStringLiteral("77");
        report.plan.publishedAt = QDateTime::currentDateTime();
        const QString url = publish.cycleUrl(report);
        QVERIFY2(url.startsWith(QStringLiteral("https://jira.acme.com/secure/enav/#?query=")), qPrintable(url));
        QVERIFY(QUrl::fromPercentEncoding(url.toLatin1()).contains(QStringLiteral("cycleName = \"Regresión Sprint 14 · 12/05/2026\"")));
        f.settings.updateTracker([](TrackerSettings& s) { s.url.clear(); });
        QVERIFY(publish.cycleUrl(report).isEmpty());   // sin URL del gestor, sin enlace
    }

    // Cada ejecución publica sus evidencias: las de esa ejecución que sigan en disco, con su paso.
    void attachmentsAreTheEvidenceOfThatRunWithTheirStep() {
        AppFixture f;
        QTemporaryDir dir;
        auto write = [&](const QString& name) {
            const QString path = dir.filePath(name);
            QFile file(path);
            file.open(QIODevice::WriteOnly);
            file.write("PNG");
            return path;
        };
        const QString shot = write(QStringLiteral("cap_001.png"));
        const QString anterior = write(QStringLiteral("cap_009.png"));
        f.store.updateCase(QStringLiteral("TC-101"), [&](TestCase& c) {
            c.shots = {Screenshot{1, 2, QStringLiteral("cap_001.png"), shot, QStringLiteral("R-1")},
                       Screenshot{2, 0, QStringLiteral("perdida.png"), dir.filePath(QStringLiteral("perdida.png")), QStringLiteral("R-1")},
                       Screenshot{9, 1, QStringLiteral("cap_009.png"), anterior, QStringLiteral("R-0")}};
        });
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);

        publish.publish(reportWith({{QStringLiteral("TC-101"), Verdict::Superado}}), [](const PublishResult&) {});
        const QList<PublishAttachment> sent = zephyr->published[0].cases[0].attachments;
        QCOMPARE(sent.size(), 1);                          // ni la que ya no está en disco, ni la de otra ejecución
        QCOMPARE(sent[0].path, shot);
        QCOMPARE(sent[0].step, 2);
    }

    // Un bug se cuelga del paso sólo en la ejecución de la que salió. Si el caso se repitió en el ciclo
    // y se publica la última, el de la anterior va a la ejecución pero no a un paso que en ésta pudo pasar.
    void onlyBugsOfThePublishedRunAreLinkedToTheirStep() {
        AppFixture f;
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.history, f.settings, f.bugLedger);
        auto bug = [&](const QString& key, const QString& runId, int step) {
            IssueLink l;
            l.key = key;
            l.caseId = QStringLiteral("TC-101");
            l.runId = runId;
            l.planRunId = QStringLiteral("PR-0001");
            l.step = step;
            l.createdAt = QDateTime(QDate(2026, 5, 12), QTime(10, 0));
            f.bugLedger.recordIssue(l);
        };
        bug(QStringLiteral("SHOP-1"), QStringLiteral("R-1"), 2);   // de la ejecución que se publica
        bug(QStringLiteral("SHOP-2"), QStringLiteral("R-0"), 1);   // de una repetición anterior en el ciclo
        bug(QStringLiteral("SHOP-3"), QString(), 2);               // antiguo, sin ejecución anotada

        publish.publish(reportWith({{QStringLiteral("TC-101"), Verdict::Fallido}}), [](const PublishResult&) {});
        const QList<PublishDefect> defects = zephyr->published[0].cases[0].defects;
        QCOMPARE(defects.size(), 3);                       // todos van a la ejecución
        QCOMPARE(defects[0].key, QStringLiteral("SHOP-1"));
        QCOMPARE(defects[0].step, 2);
        QCOMPARE(defects[1].key, QStringLiteral("SHOP-2"));
        QCOMPARE(defects[1].step, 0);                      // a ningún paso
        QCOMPARE(defects[2].step, 2);
    }
};

QTEST_MAIN(TestPublishServiceTest)
#include "test_test_publish_service.moc"
