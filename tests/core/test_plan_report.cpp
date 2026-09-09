// PlanReport (core/models/PlanReport.h): informe calculado a partir de un PlanRun y sus registros.

#include "core/models/PlanReport.h"

#include <QtTest>

using namespace qaflow;

namespace {

/// Plan de tres casos con dos ejecutados (uno repetido) y uno pendiente.
struct Sample {
    PlanRun plan;
    RunRecord first, retry, blocked, foreign;

    Sample() {
        plan.id = QStringLiteral("PR-0001");
        plan.name = QStringLiteral("Regresión");
        plan.caseIds = {QStringLiteral("TC-1"), QStringLiteral("TC-2"), QStringLiteral("TC-3")};
        plan.startedAt = QDateTime(QDate(2026, 9, 7), QTime(10, 0));
        plan.finishedAt = plan.startedAt.addSecs(600);

        first.id = QStringLiteral("R-1"); first.caseId = QStringLiteral("TC-1"); first.caseTitle = QStringLiteral("Login"); first.planRunId = plan.id;
        first.startedAt = plan.startedAt; first.finishedAt = first.startedAt.addSecs(60); first.durationSecs = 60;
        first.verdict = Verdict::Fallido; first.plannedSteps = 2;
        first.steps = {RunRecordStep{QStringLiteral("abrir"), {}, StepResult::Pass, {}}, RunRecordStep{QStringLiteral("entrar"), {}, StepResult::Fail, QStringLiteral("500")}};

        retry = first;   // repetición posterior del mismo caso: es la que cuenta
        retry.id = QStringLiteral("R-2"); retry.startedAt = first.finishedAt; retry.finishedAt = retry.startedAt.addSecs(30); retry.durationSecs = 30;
        retry.verdict = Verdict::Superado; retry.steps[1].result = StepResult::Pass; retry.steps[1].note.clear();

        blocked.id = QStringLiteral("R-3"); blocked.caseId = QStringLiteral("TC-2"); blocked.caseTitle = QStringLiteral("Pago"); blocked.planRunId = plan.id;
        blocked.startedAt = retry.finishedAt; blocked.finishedAt = blocked.startedAt.addSecs(90); blocked.durationSecs = 90;
        blocked.verdict = Verdict::Bloqueado; blocked.plannedSteps = 3;
        blocked.steps = {RunRecordStep{QStringLiteral("pagar"), {}, StepResult::Block, QStringLiteral("pasarela caída")}};

        foreign.caseId = QStringLiteral("TC-3"); foreign.planRunId = QStringLiteral("PR-0002"); foreign.verdict = Verdict::Superado;   // de otro plan
    }

    PlanReport build() const {
        return PlanReport::build(plan, {first, retry, blocked, foreign}, [](const QString& id) {
            // El catálogo: título y los enlaces del caso (historia de Jira y Test de Zephyr).
            return PlanReport::CaseInfo{QStringLiteral("Título de ") + id, QStringLiteral("SHOP-9"),
                                        id == QStringLiteral("TC-1") ? QStringLiteral("SHOP-42") : QString()};
        });
    }
};

} // namespace

class PlanReportTest : public QObject {
    Q_OBJECT
private slots:
    void countsLatestRunPerCaseAndIgnoresOtherPlans() {
        const PlanReport r = Sample().build();
        QCOMPARE(r.total(), 3);
        QCOMPARE(r.executed, 2);
        QCOMPARE(r.passed, 1);
        QCOMPARE(r.failed, 0);
        QCOMPARE(r.blocked, 1);
        QCOMPARE(r.pending(), 1);
        QCOMPARE(r.successRate(), 50);
        QCOMPARE(r.durationSecs, 180);        // suma de todas las ejecuciones, repetición incluida
        QCOMPARE(r.rows[0].run.id, QStringLiteral("R-2"));
    }

    void pendingRowsResolveTitleThroughLookup() {
        const PlanReport r = Sample().build();
        QVERIFY(!r.rows[2].executed);
        QCOMPARE(r.rows[2].caseId, QStringLiteral("TC-3"));
        QCOMPARE(r.rows[2].title, QStringLiteral("Título de TC-3"));
    }

    void verdictIsWorstOfExecutedCases() {
        Sample s;
        QCOMPARE(static_cast<int>(s.build().verdict()), static_cast<int>(Verdict::Bloqueado));
        s.blocked.verdict = Verdict::Fallido;
        QCOMPARE(static_cast<int>(s.build().verdict()), static_cast<int>(Verdict::Fallido));
        s.blocked.verdict = Verdict::Superado;
        QCOMPARE(static_cast<int>(s.build().verdict()), static_cast<int>(Verdict::Superado));
        QCOMPARE(PlanReport{}.successRate(), 0);    // sin ejecuciones no divide por cero
    }

    void markdownHasSummaryTableAndStepDetails() {
        const QString md = Sample().build().toMarkdown();
        QVERIFY(md.startsWith(QStringLiteral("# Informe de plan · Regresión")));
        QVERIFY(md.contains(QStringLiteral("**Tasa de éxito:** 50 %")));
        QVERIFY(md.contains(QStringLiteral("| TC-1 | Login |  | Superado | 2/2 |")));
        QVERIFY(md.contains(QStringLiteral("| TC-3 | Título de TC-3 |  | Pendiente |")));
        QVERIFY(md.contains(QStringLiteral("1. [Bloqueado] pagar — _pasarela caída_")));
        // Con qué está enlazado cada caso, para que el informe pegado en un ticket lo diga.
        QVERIFY(md.contains(QStringLiteral("**Historia:** SHOP-9 · **Test:** SHOP-42")));
    }

    // Las filas llevan los enlaces del caso: la historia de Jira y el Test de Zephyr.
    void rowsCarryTheLinksOfTheirCase() {
        const PlanReport r = Sample().build();
        QCOMPARE(r.rows[0].caseId, QStringLiteral("TC-1"));
        QCOMPARE(r.rows[0].jiraKey, QStringLiteral("SHOP-9"));
        QCOMPARE(r.rows[0].testKey, QStringLiteral("SHOP-42"));
        QVERIFY(r.rows[1].testKey.isEmpty());        // el caso que aún no tiene Test
        QVERIFY(!r.rows[2].executed);                // y el pendiente también los trae
        QCOMPARE(r.rows[2].jiraKey, QStringLiteral("SHOP-9"));
    }

    // Publicado el ciclo, el informe dice en cuál de Zephyr quedaron sus resultados.
    void theMarkdownNamesTheZephyrCycleOncePublished() {
        Sample s;
        QVERIFY(!s.build().toMarkdown().contains(QStringLiteral("Ciclo de Zephyr")));
        s.plan.zephyrCycleId = QStringLiteral("77");
        s.plan.publishedAt = QDateTime(QDate(2026, 5, 12), QTime(12, 30));
        QVERIFY(s.build().toMarkdown().contains(QStringLiteral("- **Ciclo de Zephyr:** 77 · publicado el 12/05/2026 12:30")));
    }
};

QTEST_APPLESS_MAIN(PlanReportTest)
#include "test_plan_report.moc"
