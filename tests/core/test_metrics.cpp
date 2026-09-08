// metrics:: (core/models/Metrics.h): tasa de éxito por suite y evolución entre ciclos.

#include "core/models/Metrics.h"

#include <QtTest>

using namespace qaflow;

namespace {
TestCase caseWith(const QString& id, const QString& suite, RunOutcome outcome) {
    TestCase c;
    c.id = id; c.suite = suite;
    c.lastRun = LastRun{outcome, outcome == RunOutcome::None ? QDateTime() : QDateTime::currentDateTime()};
    return c;
}
RunRecord run(const QString& caseId, const QString& planRunId, Verdict v, qint64 secs, const QDateTime& at) {
    RunRecord r;
    r.caseId = caseId; r.planRunId = planRunId; r.verdict = v; r.durationSecs = secs; r.finishedAt = at; r.startedAt = at.addSecs(-secs);
    return r;
}
PlanRun plan(const QString& id, const QString& planId, const QStringList& cases, const QDateTime& finished) {
    PlanRun p;
    p.id = id; p.planId = planId; p.name = QStringLiteral("Plan"); p.caseIds = cases; p.startedAt = finished.addSecs(-3600); p.finishedAt = finished;
    return p;
}
} // namespace

class MetricsTest : public QObject {
    Q_OBJECT
private slots:
    void summaryCountsLastOutcomes() {
        const QList<TestCase> cases{caseWith("TC-1", "A", RunOutcome::Passed), caseWith("TC-2", "A", RunOutcome::Failed),
                                    caseWith("TC-3", "B", RunOutcome::Blocked), caseWith("TC-4", "B", RunOutcome::None)};
        const MetricsSummary s = metrics::summary(cases);
        QCOMPARE(s.cases, 4);
        QCOMPARE(s.executed(), 3);
        QCOMPARE(s.passed, 1);
        QCOMPARE(s.successRate(), 33);
        QCOMPARE(metrics::summary({}).successRate(), 0);
    }

    void bySuiteGroupsAndSortsByName() {
        const QList<TestCase> cases{caseWith("TC-1", "Checkout", RunOutcome::Passed), caseWith("TC-2", "Auth", RunOutcome::Passed),
                                    caseWith("TC-3", "Auth", RunOutcome::Failed), caseWith("TC-4", "Auth", RunOutcome::None),
                                    caseWith("TC-5", "", RunOutcome::Blocked)};
        const auto suites = metrics::bySuite(cases);
        QCOMPARE(suites.size(), 3);
        QCOMPARE(suites[0].suite, QString());          // sin suite primero
        QCOMPARE(suites[1].suite, QStringLiteral("Auth"));
        QCOMPARE(suites[1].cases, 3);
        QCOMPARE(suites[1].executed(), 2);
        QCOMPARE(suites[1].notRun(), 1);
        QCOMPARE(suites[1].successRate(), 50);
        QCOMPARE(suites[2].suite, QStringLiteral("Checkout"));
        QCOMPARE(suites[2].successRate(), 100);
        QCOMPARE(suites[0].successRate(), 0);         // bloqueado: ejecutado pero no superado
    }

    void cyclesAreFinishedPlansInChronologicalOrder() {
        const QDateTime t0 = QDateTime(QDate(2026, 3, 1), QTime(10, 0));
        RunHistory h;
        h.plans << plan("PR-2", "PL-1", {"TC-1", "TC-2"}, t0.addDays(7))
                << plan("PR-1", "PL-1", {"TC-1", "TC-2"}, t0)
                << plan("PR-3", "PL-2", {"TC-3"}, t0.addDays(3));
        PlanRun open = plan("PR-4", "PL-1", {"TC-1"}, QDateTime());
        h.plans << open;   // en curso: no cuenta
        h.runs << run("TC-1", "PR-1", Verdict::Superado, 60, t0.addSecs(-100))
               << run("TC-2", "PR-1", Verdict::Fallido, 30, t0.addSecs(-50))
               << run("TC-1", "PR-2", Verdict::Superado, 45, t0.addDays(7).addSecs(-100))
               << run("TC-3", "PR-3", Verdict::Superado, 10, t0.addDays(3));

        const auto all = metrics::cycles(h);
        QCOMPARE(all.size(), 3);
        QCOMPARE(all[0].planRunId, QStringLiteral("PR-1"));
        QCOMPARE(all[1].planRunId, QStringLiteral("PR-3"));
        QCOMPARE(all[2].planRunId, QStringLiteral("PR-2"));
        QCOMPARE(all[0].successRate(), 50);
        QCOMPARE(all[0].durationSecs, 90);
        QCOMPARE(all[2].executed, 1);
        QCOMPARE(all[2].total, 2);
        QCOMPARE(all[2].coverage(), 50);
        QCOMPARE(all[2].successRate(), 100);

        const auto onlyPl1 = metrics::cycles(h, QStringLiteral("PL-1"));
        QCOMPARE(onlyPl1.size(), 2);
        QCOMPARE(onlyPl1.last().planRunId, QStringLiteral("PR-2"));
    }

    void trendComparesLastTwoCycles() {
        QList<CycleMetrics> c;
        QVERIFY(!metrics::trend(c));
        CycleMetrics a; a.executed = 4; a.passed = 2;     // 50 %
        CycleMetrics b; b.executed = 5; b.passed = 4;     // 80 %
        c << a;
        QVERIFY(!metrics::trend(c));
        c << b;
        QCOMPARE(*metrics::trend(c), 30);
        c << a;
        QCOMPARE(*metrics::trend(c), -30);
    }
};

QTEST_APPLESS_MAIN(MetricsTest)
#include "test_metrics.moc"
