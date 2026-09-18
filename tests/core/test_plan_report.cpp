// PlanReport (core/models/PlanReport.h): informe calculado a partir de un PlanRun y sus registros.

#include "core/models/PlanReport.h"

#include <QtTest>

using namespace qaflow;

namespace {

/// Bug ya creado en el gestor, enlazado a su caso y a su paso.
IssueLink bug(const QString& key, const QString& caseId, int step, const QDateTime& at, bool resolved = false,
              const QString& issueType = QStringLiteral("Bug")) {
    IssueLink b;
    b.key = key; b.caseId = caseId; b.step = step; b.createdAt = at; b.resolved = resolved;
    b.title = QStringLiteral("Fallo de ") + caseId;
    b.severity = QStringLiteral("Mayor");
    b.issueType = issueType;
    return b;
}

/// Plan de tres casos con dos ejecutados (uno repetido) y uno pendiente.
struct Sample {
    PlanRun plan;
    RunRecord first, retry, blocked, foreign;
    QList<IssueLink> bugs;

    Sample() {
        plan.id = QStringLiteral("PR-0001");
        plan.name = QStringLiteral("Regresión");
        plan.caseIds = {QStringLiteral("TC-1"), QStringLiteral("TC-2"), QStringLiteral("TC-3")};
        plan.startedAt = QDateTime(QDate(2026, 9, 7), QTime(10, 0));
        plan.finishedAt = plan.startedAt.addSecs(600);

        first.id = QStringLiteral("R-1"); first.caseId = QStringLiteral("TC-1"); first.caseTitle = QStringLiteral("Login"); first.planRunId = plan.id;
        first.startedAt = plan.startedAt; first.finishedAt = first.startedAt.addSecs(60); first.durationSecs = 60;
        first.verdict = Verdict::Fallido; first.plannedSteps = 2;
        first.steps = {RunRecordStep{QStringLiteral("abrir"), {}, {}, StepResult::Pass, {}},
                       RunRecordStep{QStringLiteral("entrar"), {}, {}, StepResult::Fail, QStringLiteral("500")}};

        retry = first;   // repetición posterior del mismo caso: es la que cuenta
        retry.id = QStringLiteral("R-2"); retry.startedAt = first.finishedAt; retry.finishedAt = retry.startedAt.addSecs(30); retry.durationSecs = 30;
        retry.verdict = Verdict::Superado; retry.steps[1].result = StepResult::Pass; retry.steps[1].note.clear();
        retry.testKey = QStringLiteral("SHOP-42");   // el Test que se creó para esta ejecución al publicar

        blocked.id = QStringLiteral("R-3"); blocked.caseId = QStringLiteral("TC-2"); blocked.caseTitle = QStringLiteral("Pago"); blocked.planRunId = plan.id;
        blocked.startedAt = retry.finishedAt; blocked.finishedAt = blocked.startedAt.addSecs(90); blocked.durationSecs = 90;
        blocked.verdict = Verdict::Bloqueado; blocked.plannedSteps = 3;
        blocked.steps = {RunRecordStep{QStringLiteral("pagar"), {}, {}, StepResult::Block, QStringLiteral("pasarela caída")}};

        foreign.caseId = QStringLiteral("TC-3"); foreign.planRunId = QStringLiteral("PR-0002"); foreign.verdict = Verdict::Superado;   // de otro plan

        // El libro de bugs del proyecto entero: dos del ciclo y dos que no son suyos.
        bugs = {bug(QStringLiteral("SHOP-11"), QStringLiteral("TC-1"), 2, plan.startedAt.addSecs(50)),
                // Probando no sólo salen errores: también lo que se pide cambiar.
                bug(QStringLiteral("SHOP-13"), QStringLiteral("TC-1"), 1, plan.startedAt.addSecs(100), false, QStringLiteral("Improvement")),
                bug(QStringLiteral("SHOP-12"), QStringLiteral("TC-2"), 1, plan.startedAt.addSecs(400), true),
                bug(QStringLiteral("SHOP-90"), QStringLiteral("TC-1"), 1, plan.startedAt.addDays(-3)),    // de antes del ciclo
                bug(QStringLiteral("SHOP-91"), QStringLiteral("TC-9"), 1, plan.startedAt.addSecs(60))};   // de un caso que no es del plan
    }

    PlanReport build() const {
        return PlanReport::build(plan, {first, retry, blocked, foreign}, [](const QString& id) {
            // El catálogo: título y la historia de Jira del caso.
            return PlanReport::CaseInfo{QStringLiteral("Título de ") + id, QStringLiteral("SHOP-9")};
        }, bugs);
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

    // Las filas llevan la historia de Jira del caso y el Test de Zephyr de la ejecución publicada.
    void rowsCarryTheStoryOfTheirCaseAndTheTestOfTheirRun() {
        const PlanReport r = Sample().build();
        QCOMPARE(r.rows[0].caseId, QStringLiteral("TC-1"));
        QCOMPARE(r.rows[0].jiraKey, QStringLiteral("SHOP-9"));
        QCOMPARE(r.rows[0].testKey, QStringLiteral("SHOP-42"));   // el de la ejecución que cuenta (la repetición)
        QVERIFY(r.rows[1].testKey.isEmpty());        // ejecución que aún no se publicó
        QVERIFY(!r.rows[2].executed);                // el pendiente trae la historia pero no tiene Test
        QCOMPARE(r.rows[2].jiraKey, QStringLiteral("SHOP-9"));
        QVERIFY(r.rows[2].testKey.isEmpty());
    }

    // Los bugs del informe son los de sus casos reportados mientras corría el ciclo: errores y mejoras.
    void bugsAreThoseReportedDuringTheCycleOnItsCases() {
        const PlanReport r = Sample().build();
        QCOMPARE(r.bugCount(), 3);
        QCOMPARE(r.openBugCount(), 2);
        QCOMPARE(r.rows[0].bugs.size(), 2);
        QCOMPARE(r.rows[0].bugs[0].key, QStringLiteral("SHOP-13"));   // el más reciente primero
        QCOMPARE(r.rows[0].bugs[1].key, QStringLiteral("SHOP-11"));   // el de hace tres días no cuenta
        QCOMPARE(r.rows[1].bugs.size(), 1);
        QVERIFY(r.rows[2].bugs.isEmpty());
        QCOMPARE(r.bugs().size(), 3);
        // Y se cuentan por tipo: lo que está mal y lo que se pide cambiar no es lo mismo.
        const auto byType = r.bugCountsByType();
        QCOMPARE(byType.size(), 2);
        QCOMPARE(byType[0], qMakePair(QStringLiteral("Improvement"), 1));
        QCOMPARE(byType[1], qMakePair(QStringLiteral("Bug"), 2));
        // Sin el libro de bugs el informe sale igual, sólo que sin ellos.
        Sample s;
        s.bugs.clear();
        QCOMPARE(s.build().bugCount(), 0);
    }

    // El bug dice de qué ejecución salió: eso manda sobre las fechas, que sólo valen para los que no
    // lo anotan (los de antes y los traídos del gestor).
    void aBugBelongsToTheCycleItWasFoundIn() {
        Sample s;
        IssueLink mine = bug(QStringLiteral("SHOP-20"), QStringLiteral("TC-1"), 1, s.plan.startedAt.addDays(-3));
        mine.planRunId = s.plan.id;
        mine.runId = QStringLiteral("R-1");
        QVERIFY2(PlanReport::foundIn(s.plan, mine), "salió de este ciclo aunque la fecha diga otra cosa");

        IssueLink other = bug(QStringLiteral("SHOP-21"), QStringLiteral("TC-1"), 1, s.plan.startedAt.addSecs(30));
        other.planRunId = QStringLiteral("PR-0009");
        QVERIFY2(!PlanReport::foundIn(s.plan, other), "salió de otro ciclo, aunque coincidan las fechas");

        IssueLink loose = bug(QStringLiteral("SHOP-22"), QStringLiteral("TC-1"), 1, s.plan.startedAt.addSecs(30));
        loose.runId = QStringLiteral("R-77");   // ejecución suelta, fuera de ciclo
        QVERIFY(!PlanReport::foundIn(s.plan, loose));

        // Y el informe los coloca en la fila de su caso.
        s.bugs = {mine, other, loose};
        const PlanReport r = s.build();
        QCOMPARE(r.bugCount(), 1);
        QCOMPARE(r.rows[0].bugs.size(), 1);
        QCOMPARE(r.rows[0].bugs[0].key, QStringLiteral("SHOP-20"));
    }

    // Dentro del ciclo, cada ejecución enseña lo que salió de ella: repetir un caso no hereda sus bugs.
    void aBugBelongsToTheRunItWasFoundIn() {
        Sample s;
        IssueLink first = bug(QStringLiteral("SHOP-30"), QStringLiteral("TC-1"), 2, s.first.startedAt.addSecs(10));
        first.runId = s.first.id;
        first.planRunId = s.plan.id;
        QVERIFY(PlanReport::foundIn(s.first, first));
        QVERIFY(!PlanReport::foundIn(s.retry, first));

        // Los antiguos, sin ejecución anotada, se sitúan por caso y por la ventana de la ejecución.
        const IssueLink legacy = bug(QStringLiteral("SHOP-31"), QStringLiteral("TC-1"), 2, s.first.startedAt.addSecs(10));
        QVERIFY(PlanReport::foundIn(s.first, legacy));
        QVERIFY(!PlanReport::foundIn(s.blocked, legacy));   // otro caso
    }

    // El margen tras el cierre: el parte se escribe justo después de ver el fallo.
    void aBugWrittenRightAfterTheCycleStillBelongsToIt() {
        Sample s;
        QVERIFY(PlanReport::reportedDuring(s.plan, bug(QStringLiteral("X"), QStringLiteral("TC-1"), 1, s.plan.finishedAt.addSecs(600))));
        QVERIFY(!PlanReport::reportedDuring(s.plan, bug(QStringLiteral("X"), QStringLiteral("TC-1"), 1, s.plan.finishedAt.addSecs(7200))));
        QVERIFY(!PlanReport::reportedDuring(s.plan, bug(QStringLiteral("X"), QStringLiteral("TC-1"), 1, s.plan.startedAt.addSecs(-1))));
        // Un ciclo en curso admite todo lo reportado desde que arrancó.
        s.plan.finishedAt = QDateTime();
        QVERIFY(PlanReport::reportedDuring(s.plan, bug(QStringLiteral("X"), QStringLiteral("TC-1"), 1, QDateTime::currentDateTime())));
    }

    void markdownListsTheBugsOfTheCycle() {
        const QString md = Sample().build().toMarkdown();
        QVERIFY2(md.contains(QStringLiteral("- **Bugs encontrados:** 3 · 2 abiertos · 1 Improvement · 2 Bug")), qPrintable(md));
        QVERIFY(md.contains(QStringLiteral("## Bugs encontrados · 3")));
        QVERIFY(md.contains(QStringLiteral("| SHOP-11 | Bug | TC-1 | 2 | Fallo de TC-1 | Mayor | Abierto |")));
        QVERIFY(md.contains(QStringLiteral("| SHOP-13 | Improvement | TC-1 | 1 | Fallo de TC-1 | Mayor | Abierto |")));
        QVERIFY(md.contains(QStringLiteral("| SHOP-12 | Bug | TC-2 | 1 | Fallo de TC-2 | Mayor | Cerrado |")));
        QVERIFY(!md.contains(QStringLiteral("SHOP-90")));   // el de antes del ciclo no sale
        // Y cada caso nombra los suyos junto a sus enlaces.
        QVERIFY(md.contains(QStringLiteral("**Bugs:** SHOP-13 (paso 1), SHOP-11 (paso 2)")));

        Sample s;
        s.bugs.clear();
        QVERIFY(!s.build().toMarkdown().contains(QStringLiteral("Bugs encontrados")));
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
