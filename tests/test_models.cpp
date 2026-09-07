#include "core/models/BugReport.h"
#include "core/models/PlanReport.h"
#include "core/models/RunHistory.h"
#include "core/models/TestCase.h"
#include "core/models/TestRun.h"

#include <QtTest>

using namespace qaflow;

class ModelsTest : public QObject {
    Q_OBJECT
private slots:
    void lastRunLabel() {
        const QDateTime now(QDate(2026, 9, 7), QTime(12, 0));
        QCOMPARE(LastRun{}.label(now), QStringLiteral("Sin ejecutar"));
        QCOMPARE((LastRun{RunOutcome::Passed, now.addSecs(-30)}).label(now), QStringLiteral("Pasó · ahora"));
        QCOMPARE((LastRun{RunOutcome::Failed, now.addSecs(-600)}).label(now), QStringLiteral("Falló · hace 10 min"));
        QCOMPARE((LastRun{RunOutcome::Passed, now.addDays(-1)}).label(now), QStringLiteral("Pasó · ayer"));
        QCOMPARE((LastRun{RunOutcome::Passed, now.addDays(-2)}).label(now), QStringLiteral("Pasó · hace 2 d"));
        QCOMPARE((LastRun{RunOutcome::Blocked, now.addSecs(-7200)}).label(now), QStringLiteral("Bloqueado · hace 2 h"));
    }

    void readyToBeMarkedListo() {
        TestCase c;
        QVERIFY(!c.readyToBeMarkedListo());
        c.title = QStringLiteral("x");
        c.steps.append(TestStep{QStringLiteral("a"), QString()});
        QVERIFY(!c.readyToBeMarkedListo());
        c.steps[0].expected = QStringLiteral("b");
        QVERIFY(c.readyToBeMarkedListo());
    }

    void runVerdict() {
        RunState r;
        r.results = {StepRecord{StepResult::Pass, {}}, StepRecord{StepResult::Pass, {}}};
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Superado));
        r.results.append(StepRecord{StepResult::Fail, {}});
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Fallido));
        QCOMPARE(r.firstFailIndex(), 2);
        r.results.append(StepRecord{StepResult::Block, {}});
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Bloqueado));
    }

    void bugValidation() {
        BugReport b;
        QVERIFY(!b.isValid());
        b.title = QStringLiteral("t");
        QVERIFY(!b.isValid());
        b.actual = QStringLiteral("a");
        QVERIFY(b.isValid());
        QVERIFY(b.jiraDescription().contains(QStringLiteral("h3. Resultado actual")));
    }

    void durationFormat() {
        QCOMPARE(formatDuration(45), QStringLiteral("45 s"));
        QCOMPARE(formatDuration(252), QStringLiteral("4 min 12 s"));
        QCOMPARE(formatDuration(3900), QStringLiteral("1 h 05 min"));
        QCOMPARE(formatDuration(-3), QStringLiteral("0 s"));
    }

    void planReportBuild() {
        PlanRun plan;
        plan.id = QStringLiteral("PR-0001");
        plan.name = QStringLiteral("Regresión");
        plan.caseIds = {QStringLiteral("TC-1"), QStringLiteral("TC-2"), QStringLiteral("TC-3")};
        plan.startedAt = QDateTime(QDate(2026, 9, 7), QTime(10, 0));
        plan.finishedAt = plan.startedAt.addSecs(600);

        RunRecord a;
        a.id = QStringLiteral("R-1"); a.caseId = QStringLiteral("TC-1"); a.caseTitle = QStringLiteral("Login"); a.planRunId = plan.id;
        a.startedAt = plan.startedAt; a.finishedAt = a.startedAt.addSecs(60); a.verdict = Verdict::Fallido; a.plannedSteps = 2;
        a.steps = {RunRecordStep{QStringLiteral("abrir"), {}, StepResult::Pass, {}}, RunRecordStep{QStringLiteral("entrar"), {}, StepResult::Fail, QStringLiteral("500")}};
        RunRecord a2 = a; // repetición posterior del mismo caso: cuenta la última
        a2.id = QStringLiteral("R-2"); a2.startedAt = a.finishedAt; a2.finishedAt = a2.startedAt.addSecs(30); a2.verdict = Verdict::Superado;
        a2.steps[1].result = StepResult::Pass; a2.steps[1].note.clear();
        RunRecord b;
        b.id = QStringLiteral("R-3"); b.caseId = QStringLiteral("TC-2"); b.caseTitle = QStringLiteral("Pago"); b.planRunId = plan.id;
        b.startedAt = a2.finishedAt; b.finishedAt = b.startedAt.addSecs(90); b.verdict = Verdict::Bloqueado; b.plannedSteps = 3;
        b.steps = {RunRecordStep{QStringLiteral("pagar"), {}, StepResult::Block, QStringLiteral("pasarela caída")}};
        RunRecord other; // de otro plan: se ignora
        other.caseId = QStringLiteral("TC-3"); other.planRunId = QStringLiteral("PR-0002"); other.verdict = Verdict::Superado;

        const PlanReport r = PlanReport::build(plan, {a, a2, b, other}, [](const QString& id) { return QStringLiteral("Título de ") + id; });
        QCOMPARE(r.total(), 3);
        QCOMPARE(r.executed, 2);
        QCOMPARE(r.passed, 1);
        QCOMPARE(r.failed, 0);
        QCOMPARE(r.blocked, 1);
        QCOMPARE(r.pending(), 1);
        QCOMPARE(r.successRate(), 50);
        QCOMPARE(r.durationSecs, 180);
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Bloqueado));
        QCOMPARE(r.rows[0].run.id, QStringLiteral("R-2"));
        QVERIFY(!r.rows[2].executed);
        QCOMPARE(r.rows[2].title, QStringLiteral("Título de TC-3"));

        const QString md = r.toMarkdown();
        QVERIFY(md.startsWith(QStringLiteral("# Informe de plan · Regresión")));
        QVERIFY(md.contains(QStringLiteral("| TC-1 | Login |  | Superado | 2/2 |")));
        QVERIFY(md.contains(QStringLiteral("| TC-3 | Título de TC-3 |  | Pendiente |")));
        QVERIFY(md.contains(QStringLiteral("1. [Bloqueado] pagar — _pasarela caída_")));
        QVERIFY(md.contains(QStringLiteral("**Tasa de éxito:** 50 %")));
    }

    void enumRoundTrip() {
        for (auto p : {Priority::Alta, Priority::Media, Priority::Baja}) QCOMPARE(static_cast<int>(priorityFromString(toString(p))), static_cast<int>(p));
        for (auto s : {CaseStatus::Listo, CaseStatus::Borrador, CaseStatus::Obsoleto}) QCOMPARE(static_cast<int>(statusFromString(toString(s))), static_cast<int>(s));
        for (auto v : {Verdict::Superado, Verdict::Fallido, Verdict::Bloqueado}) QCOMPARE(static_cast<int>(verdictFromString(toString(v))), static_cast<int>(v));
        for (auto r : {StepResult::Pass, StepResult::Fail, StepResult::Block}) QCOMPARE(static_cast<int>(stepResultFromString(toString(r))), static_cast<int>(r));
    }
};

QTEST_APPLESS_MAIN(ModelsTest)
#include "test_models.moc"
