#include "core/models/BugReport.h"
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

    void enumRoundTrip() {
        for (auto p : {Priority::Alta, Priority::Media, Priority::Baja}) QCOMPARE(static_cast<int>(priorityFromString(toString(p))), static_cast<int>(p));
        for (auto s : {CaseStatus::Listo, CaseStatus::Borrador, CaseStatus::Obsoleto}) QCOMPARE(static_cast<int>(statusFromString(toString(s))), static_cast<int>(s));
    }
};

QTEST_APPLESS_MAIN(ModelsTest)
#include "test_models.moc"
