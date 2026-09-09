// TestPublishService (application/): traduce el informe de un ciclo de plan a lo que espera la
// herramienta de gestión de pruebas. Cubre qué casos entran, de dónde sale la clave del Test y
// cómo viajan las evidencias con su paso.

#include "support/AppFixture.h"
#include "support/FakeTestManagement.h"

#include "application/TestPublishService.h"

#include <QTemporaryDir>
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
        TestPublishService publish(zephyr, f.store, f.settings);
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

    void onlyExecutedCasesTravelAndTheyCarryTheirTestKey() {
        AppFixture f;
        f.store.updateCase(QStringLiteral("TC-101"), [](TestCase& c) { c.testKey = QStringLiteral("SHOP-42"); });
        f.store.updateCase(QStringLiteral("TC-102"), [](TestCase& c) { c.testKey = QStringLiteral("SHOP-43"); });
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; s.zephyrVersion = QStringLiteral("2.3.0"); });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.settings);

        const PlanReport report = reportWith({{QStringLiteral("TC-101"), Verdict::Superado},
                                              {QStringLiteral("TC-102"), Verdict::Fallido}},
                                             QStringLiteral("TC-103"));
        PublishResult out;
        publish.publish(report, [&](const PublishResult& r) { out = r; });
        QVERIFY(out.ok);
        QCOMPARE(zephyr->published.size(), 1);
        const PublishRequest& req = zephyr->published[0];
        QCOMPARE(req.cases.size(), 2);                     // el pendiente no se publica
        QCOMPARE(req.cases[0].testKey, QStringLiteral("SHOP-42"));
        QCOMPARE(req.cases[1].testKey, QStringLiteral("SHOP-43"));
        QCOMPARE(static_cast<int>(req.cases[1].verdict), static_cast<int>(Verdict::Fallido));
        QCOMPARE(req.cases[1].steps.size(), 2);
        QCOMPARE(req.versionName, QStringLiteral("2.3.0"));
        QVERIFY(req.cycleName.startsWith(QStringLiteral("Regresión Sprint 14")));
        QVERIFY(req.cycleName.contains(QStringLiteral("12/05/2026")));   // el ciclo lleva la fecha
        QCOMPARE(req.startedAt, report.plan.startedAt);
    }

    void casesWithoutTestKeyAreListedBeforePublishing() {
        AppFixture f;
        f.store.updateCase(QStringLiteral("TC-101"), [](TestCase& c) { c.testKey = QStringLiteral("SHOP-42"); });
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        TestPublishService publish(std::make_shared<FakeTestManagement>(), f.store, f.settings);
        const PlanReport report = reportWith({{QStringLiteral("TC-101"), Verdict::Superado},
                                              {QStringLiteral("TC-102"), Verdict::Superado}});
        QCOMPARE(publish.casesWithoutTestKey(report), QStringList{QStringLiteral("TC-102")});
    }

    // Las evidencias que existen en disco viajan con el paso al que se asignaron.
    void attachmentsTravelWithTheirStep() {
        AppFixture f;
        QTemporaryDir dir;
        const QString shot = dir.filePath(QStringLiteral("cap_001.png"));
        { QFile file(shot); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("PNG"); }
        f.store.updateCase(QStringLiteral("TC-101"), [&](TestCase& c) {
            c.testKey = QStringLiteral("SHOP-42");
            c.shots = {Screenshot{1, 2, QStringLiteral("cap_001.png"), shot},
                       Screenshot{2, 0, QStringLiteral("perdida.png"), dir.filePath(QStringLiteral("perdida.png"))}};
        });
        f.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        auto zephyr = std::make_shared<FakeTestManagement>();
        TestPublishService publish(zephyr, f.store, f.settings);

        publish.publish(reportWith({{QStringLiteral("TC-101"), Verdict::Superado}}), [](const PublishResult&) {});
        const QList<PublishAttachment> sent = zephyr->published[0].cases[0].attachments;
        QCOMPARE(sent.size(), 1);                          // la que ya no está en disco no se envía
        QCOMPARE(sent[0].path, shot);
        QCOMPARE(sent[0].step, 2);
    }
};

QTEST_MAIN(TestPublishServiceTest)
#include "test_test_publish_service.moc"
