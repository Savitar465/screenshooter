#include "core/models/BugReport.h"
#include "core/models/CaseFilter.h"
#include "core/models/CaseFormats.h"
#include "core/models/PlanReport.h"
#include "core/models/RunHistory.h"
#include "core/models/TestCase.h"
#include "core/models/TestRun.h"

#include <QJsonDocument>
#include <QJsonObject>
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
        a.startedAt = plan.startedAt; a.finishedAt = a.startedAt.addSecs(60); a.durationSecs = 60; a.verdict = Verdict::Fallido; a.plannedSteps = 2;
        a.steps = {RunRecordStep{QStringLiteral("abrir"), {}, StepResult::Pass, {}}, RunRecordStep{QStringLiteral("entrar"), {}, StepResult::Fail, QStringLiteral("500")}};
        RunRecord a2 = a; // repetición posterior del mismo caso: cuenta la última
        a2.id = QStringLiteral("R-2"); a2.startedAt = a.finishedAt; a2.finishedAt = a2.startedAt.addSecs(30); a2.durationSecs = 30; a2.verdict = Verdict::Superado;
        a2.steps[1].result = StepResult::Pass; a2.steps[1].note.clear();
        RunRecord b;
        b.id = QStringLiteral("R-3"); b.caseId = QStringLiteral("TC-2"); b.caseTitle = QStringLiteral("Pago"); b.planRunId = plan.id;
        b.startedAt = a2.finishedAt; b.finishedAt = b.startedAt.addSecs(90); b.durationSecs = 90; b.verdict = Verdict::Bloqueado; b.plannedSteps = 3;
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

    void tagsParsing() {
        QCOMPARE(parseTags(QStringLiteral(" smoke, regresión ,, Smoke ,api")), (QStringList{QStringLiteral("smoke"), QStringLiteral("regresión"), QStringLiteral("api")}));
        QVERIFY(parseTags(QString()).isEmpty());
    }

    void caseFilter() {
        TestCase c;
        c.id = QStringLiteral("TC-7"); c.title = QStringLiteral("Pago con tarjeta"); c.suite = QStringLiteral("Checkout");
        c.priority = Priority::Alta; c.status = CaseStatus::Listo; c.tags = {QStringLiteral("smoke")}; c.component = QStringLiteral("Carrito");
        c.jiraKey = QStringLiteral("SHOP-12"); c.lastRun = LastRun{RunOutcome::Failed, QDateTime::currentDateTime()};
        CaseFilter f;
        QVERIFY(f.isEmpty());
        QVERIFY(f.matches(c));
        f.text = QStringLiteral("SMOKE");  QVERIFY(f.matches(c));
        f.text = QStringLiteral("carrito"); QVERIFY(f.matches(c));
        f.text = QStringLiteral("shop-12"); QVERIFY(f.matches(c));
        f.text = QStringLiteral("perfil"); QVERIFY(!f.matches(c));
        f.text.clear();
        f.suite = QStringLiteral("Perfil"); QVERIFY(!f.matches(c));
        f.suite = QStringLiteral("Checkout"); QVERIFY(f.matches(c));
        f.status = CaseStatus::Borrador; QVERIFY(!f.matches(c));
        f.status = CaseStatus::Listo; QVERIFY(f.matches(c));
        f.priority = Priority::Baja; QVERIFY(!f.matches(c));
        f.priority.reset();
        f.outcome = RunOutcome::None; QVERIFY(!f.matches(c));
        f.outcome = RunOutcome::Failed; QVERIFY(f.matches(c));
        QVERIFY(!f.isEmpty());
    }

    void jsonRoundTripKeepsMetadata() {
        TestCase c;
        c.id = QStringLiteral("TC-1"); c.title = QStringLiteral("T"); c.suite = QStringLiteral("S");
        c.tags = {QStringLiteral("a"), QStringLiteral("b")}; c.component = QStringLiteral("Comp"); c.jiraKey = QStringLiteral("SHOP-1");
        c.steps = {TestStep{QStringLiteral("acción"), QStringLiteral("esperado")}};
        c.shots = {Screenshot{3, 1, QStringLiteral("cap.png"), QStringLiteral("/x/cap.png")}};
        c.lastRun = LastRun{RunOutcome::Blocked, QDateTime(QDate(2026, 9, 7), QTime(10, 0))};
        const QByteArray bytes = QJsonDocument(formats::casesToJson({c})).toJson();
        const auto back = formats::casesFromJson(bytes);
        QVERIFY(back.has_value());
        QCOMPARE(back->size(), 1);
        const TestCase& r = back->first();
        QCOMPARE(r.tags, c.tags);
        QCOMPARE(r.component, c.component);
        QCOMPARE(r.jiraKey, c.jiraKey);
        QCOMPARE(r.shots.size(), 1);
        QCOMPARE(static_cast<int>(r.lastRun.outcome), static_cast<int>(RunOutcome::Blocked));
        // Exportación para compartir: sin capturas; acepta también {"cases": [...]}.
        const QByteArray shared = QJsonDocument(QJsonObject{{"cases", formats::casesToJson({c}, false)}}).toJson();
        QVERIFY(formats::casesFromJson(shared)->first().shots.isEmpty());
        QString err;
        QVERIFY(!formats::casesFromJson("{not json", &err).has_value());
        QVERIFY(!err.isEmpty());
    }

    void csvRoundTrip() {
        TestCase a;
        a.id = QStringLiteral("TC-1"); a.title = QStringLiteral("Título, con coma"); a.suite = QStringLiteral("S"); a.priority = Priority::Alta;
        a.status = CaseStatus::Listo; a.tags = {QStringLiteral("smoke"), QStringLiteral("api")}; a.component = QStringLiteral("C"); a.jiraKey = QStringLiteral("SHOP-9");
        a.preconditions = QStringLiteral("línea 1\nlínea 2");
        a.steps = {TestStep{QStringLiteral("Pulsar \"OK\""), QStringLiteral("Cierra")}, TestStep{QStringLiteral("Otro"), QStringLiteral("Más")}};
        TestCase b;
        b.id = QStringLiteral("TC-2"); b.title = QStringLiteral("Sin pasos");
        const QString csv = formats::casesToCsv({a, b});
        QVERIFY(csv.startsWith(QStringLiteral("id,title,suite,priority,status,tags,component,jira,preconditions,step,action,expected\n")));
        const auto back = formats::casesFromCsv(csv);
        QVERIFY(back.has_value());
        QCOMPARE(back->size(), 2);
        const TestCase& r = back->first();
        QCOMPARE(r.title, a.title);
        QCOMPARE(r.tags, a.tags);
        QCOMPARE(r.preconditions, a.preconditions);
        QCOMPARE(r.steps.size(), 2);
        QCOMPARE(r.steps[0].action, QStringLiteral("Pulsar \"OK\""));
        QCOMPARE(static_cast<int>(r.priority), static_cast<int>(Priority::Alta));
        QVERIFY(back->last().steps.isEmpty());
        QString err;
        QVERIFY(!formats::casesFromCsv(QStringLiteral("foo,bar\n1,2\n"), &err).has_value());
        QVERIFY(err.contains(QStringLiteral("id")));
    }

    void markdownExport() {
        TestCase c;
        c.id = QStringLiteral("TC-1"); c.title = QStringLiteral("Login"); c.suite = QStringLiteral("Auth"); c.jiraKey = QStringLiteral("SHOP-3");
        c.steps = {TestStep{QStringLiteral("Abrir | pantalla"), QStringLiteral("Se ve")}};
        const QString md = formats::casesToMarkdown({c});
        QVERIFY(md.contains(QStringLiteral("## TC-1 · Login")));
        QVERIFY(md.contains(QStringLiteral("**Historia:** SHOP-3")));
        QVERIFY(md.contains(QStringLiteral("| 1 | Abrir \\| pantalla | Se ve |")));
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
