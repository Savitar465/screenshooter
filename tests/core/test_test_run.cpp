// RunState (core/models/TestRun.h) y las conversiones y formato de RunHistory.h.

#include "core/models/RunHistory.h"
#include "core/models/TestRun.h"

#include <QtTest>

using namespace qaflow;

class TestRunTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Veredicto ---------------------------------------------------------------------

    void verdictIsWorstResultSeen() {
        RunState r;
        r.results = {StepRecord{StepResult::Pass, {}}, StepRecord{StepResult::Pass, {}}};
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Superado));
        r.results.append(StepRecord{StepResult::Fail, {}});
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Fallido));
        QCOMPARE(r.firstFailIndex(), 2);
        r.results.append(StepRecord{StepResult::Block, {}});
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Bloqueado));
    }

    void skippedStepsDoNotAffectVerdict() {
        RunState r;
        r.results = {StepRecord{StepResult::Skip, {}}, StepRecord{StepResult::Pass, {}}};
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Superado));
        QCOMPARE(r.count(StepResult::Skip), 1);
        QCOMPARE(r.firstFailIndex(), -1);
    }

    void isActiveNeedsCaseAndNotFinished() {
        RunState r;
        QVERIFY(!r.isActive());
        r.caseId = QStringLiteral("TC-1");
        QVERIFY(r.isActive());
        r.finished = true;
        QVERIFY(!r.isActive());
    }

    // ---- Cronómetros -------------------------------------------------------------------

    void elapsedSecsAddsMarkedStepsAndCurrentStep() {
        const QDateTime now(QDate(2026, 9, 7), QTime(12, 0, 40));
        RunState r;
        r.caseId = QStringLiteral("TC-1");
        r.results = {StepRecord{StepResult::Pass, {}, 30}, StepRecord{StepResult::Pass, {}, 20}};
        r.stepStartedAt = now.addSecs(-10);
        r.stepElapsedSecs = 5;                        // acumulado de una sesión anterior
        QCOMPARE(r.currentStepSecs(now), 15);
        QCOMPARE(r.elapsedSecs(now), 65);
        r.finished = true;                            // terminado: el paso actual no corre
        QCOMPARE(r.currentStepSecs(now), 5);
    }

    void formatDurationUsesSecondsMinutesAndHours() {
        QCOMPARE(formatDuration(45), QStringLiteral("45 s"));
        QCOMPARE(formatDuration(252), QStringLiteral("4 min 12 s"));
        QCOMPARE(formatDuration(3900), QStringLiteral("1 h 05 min"));
        QCOMPARE(formatDuration(-3), QStringLiteral("0 s"));
    }

    // ---- Conversiones ------------------------------------------------------------------

    void verdictAndStepResultRoundTripThroughStrings() {
        for (auto v : {Verdict::Superado, Verdict::Fallido, Verdict::Bloqueado}) QCOMPARE(static_cast<int>(verdictFromString(toString(v))), static_cast<int>(v));
        for (auto r : {StepResult::Pass, StepResult::Fail, StepResult::Block, StepResult::Skip}) QCOMPARE(static_cast<int>(stepResultFromString(toString(r))), static_cast<int>(r));
        QCOMPARE(toString(StepResult::Skip), QStringLiteral("N/A"));
    }
};

QTEST_APPLESS_MAIN(TestRunTest)
#include "test_test_run.moc"
