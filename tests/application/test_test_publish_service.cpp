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
        row.run.steps = {RunRecordStep{QStringLiteral("Abrir carrito"), QStringLiteral("Se abre"), StepResult::Pass, {}, 30},
                         RunRecordStep{QStringLiteral("Aplicar cupón"), QStringLiteral("Descuenta"),
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
} // namespace

class TestPublishServiceTest : public QObject {
    Q_OBJECT
private slots:
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

    // El ciclo tiene que decir en Zephyr de qué requerimiento y de qué ronda es, y dónde se probó:
    // varios planes del mismo issue se distinguen por eso, no por el nombre del plan.
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
        QCOMPARE(publish.cycleName(report),
                 QStringLiteral("GREQ 2026997 · Rev. 2 · Regresión Sprint 14 · 12/05/2026 · QA"));

        publish.publish(report, [](const PublishResult&) {});
        const PublishRequest& sent = zephyr->published[0];
        QCOMPARE(sent.environment, QStringLiteral("QA"));   // y en el campo «environment» del ciclo
        QVERIFY(sent.description.contains(QStringLiteral("GREQ 2026997")));
        QVERIFY(sent.description.contains(QStringLiteral("Revisión 2")));
        QVERIFY(sent.description.contains(QStringLiteral("Ambiente: QA")));

        // Un ciclo suelto se queda con el nombre de siempre: no se inventa lo que no hay.
        PlanReport loose = reportWith({{QStringLiteral("TC-101"), Verdict::Superado}});
        QCOMPARE(publish.cycleName(loose), QStringLiteral("Regresión Sprint 14 · 12/05/2026"));

        // Y una continuación no puede llamarse igual que el ciclo al que continúa: lleva por dónde va.
        const QString cycleId = f.history.startPlan(QStringLiteral("Regresión Sprint 14"), {QStringLiteral("TC-101")},
                                                    QStringLiteral("PL-0001"), QStringLiteral("QA"));
        PlanReport continued = reportWith({{QStringLiteral("TC-101"), Verdict::Superado}});
        continued.plan.issueId = issueId;
        continued.plan.revision = 2;
        continued.plan.environment = QStringLiteral("QA");
        continued.plan.continuesCycleId = cycleId;
        QCOMPARE(publish.cycleName(continued),
                 QStringLiteral("GREQ 2026997 · Rev. 2 · Regresión Sprint 14 · Cont. 1 · 12/05/2026 · QA"));
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
            c.steps = {TestStep{QStringLiteral("Abrir carrito"), QStringLiteral("Se abre")},
                       TestStep{QStringLiteral("Aplicar cupón"), QStringLiteral("Descuenta")},
                       TestStep{QStringLiteral("Pagar"), QStringLiteral("Se confirma")}};
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
};

QTEST_MAIN(TestPublishServiceTest)
#include "test_test_publish_service.moc"
