// RunState (core/models/TestRun.h) y las conversiones y formato de RunHistory.h.

#include "core/models/RunHistory.h"
#include "core/models/TestRun.h"

#include <QtTest>

using namespace qaflow;

namespace {
/// Un paso ya ejecutado, que es lo que miran el veredicto y los recuentos.
StepRecord marked(StepResult r) { return StepRecord{r, {}, 0, true}; }
}   // namespace

class TestRunTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Veredicto ---------------------------------------------------------------------

    void verdictIsWorstResultSeen() {
        RunState r;
        r.results = {marked(StepResult::Pass), marked(StepResult::Pass)};
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Superado));
        r.results.append(marked(StepResult::Fail));
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Fallido));
        QCOMPARE(r.firstFailIndex(), 2);
        r.results.append(marked(StepResult::Block));
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Bloqueado));
        QCOMPARE(r.firstBlockIndex(), 3);
    }

    void skippedStepsDoNotAffectVerdict() {
        RunState r;
        r.results = {marked(StepResult::Skip), marked(StepResult::Pass)};
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Superado));
        QCOMPARE(r.count(StepResult::Skip), 1);
        QCOMPARE(r.firstFailIndex(), -1);
    }

    /// Los pasos que todavía no se han marcado no cuentan para nada: ni veredicto ni recuento.
    void pendingStepsAreIgnoredUntilTheyAreMarked() {
        RunState r;
        r.results = {marked(StepResult::Pass), StepRecord{}, marked(StepResult::Fail)};
        QCOMPARE(r.markedCount(), 2);
        QVERIFY(!r.allMarked());
        QVERIFY(!r.isMarked(1));
        QCOMPARE(r.count(StepResult::Pass), 1);
        QCOMPARE(static_cast<int>(r.verdict()), static_cast<int>(Verdict::Fallido));
        QCOMPARE(r.lastMarkedIndex(), 2);
        QCOMPARE(r.nextPending(1), 1);
        QCOMPARE(r.nextPending(2), 1);   // no queda nada por delante: vuelve al hueco de atrás
        r.results[1] = marked(StepResult::Pass);
        QVERIFY(r.allMarked());
        QCOMPARE(r.nextPending(0), -1);
    }

    /// El bug se cuelga del paso en pantalla si tiene problema; si no, del más cercano por detrás.
    void reportableStepPrefersTheStepOnScreen() {
        RunState r;
        r.results = {marked(StepResult::Fail), marked(StepResult::Pass), marked(StepResult::Block), StepRecord{}};
        r.idx = 1;
        QCOMPARE(r.reportableStepIndex(), 0);   // el de pantalla está bien: el fallo de atrás
        r.idx = 2;
        QCOMPARE(r.reportableStepIndex(), 2);   // el de pantalla está bloqueado
        r.idx = 3;
        QCOMPARE(r.reportableStepIndex(), 2);
        RunState clean;
        clean.results = {marked(StepResult::Pass)};
        QCOMPARE(clean.reportableStepIndex(), -1);
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
        r.results = {StepRecord{StepResult::Pass, {}, 30, true}, StepRecord{StepResult::Pass, {}, 20, true}, StepRecord{StepResult::Pass, {}, 5, false}};
        r.idx = 2;                                    // el paso en pantalla no se suma dos veces
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
