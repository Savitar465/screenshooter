#include "Metrics.h"

#include "core/models/PlanReport.h"

#include <QMap>
#include <algorithm>

namespace qaflow::metrics {

namespace {
template <typename T>
void tally(T& t, RunOutcome outcome) {
    switch (outcome) {
        case RunOutcome::Passed: ++t.passed; break;
        case RunOutcome::Failed: ++t.failed; break;
        case RunOutcome::Blocked: ++t.blocked; break;
        case RunOutcome::None: break;
    }
}
} // namespace

MetricsSummary summary(const QList<TestCase>& cases) {
    MetricsSummary s;
    s.cases = cases.size();
    for (const auto& c : cases) tally(s, c.lastRun.outcome);
    return s;
}

QList<SuiteMetrics> bySuite(const QList<TestCase>& cases) {
    QMap<QString, SuiteMetrics> map;
    for (const auto& c : cases) {
        SuiteMetrics& m = map[c.suite.trimmed()];
        m.suite = c.suite.trimmed();
        ++m.cases;
        tally(m, c.lastRun.outcome);
    }
    QList<SuiteMetrics> out = map.values();
    std::sort(out.begin(), out.end(), [](const SuiteMetrics& a, const SuiteMetrics& b) { return a.suite.localeAwareCompare(b.suite) < 0; });
    return out;
}

QList<CycleMetrics> cycles(const RunHistory& history, const QString& planId) {
    QList<CycleMetrics> out;
    for (const auto& plan : history.plans) {
        if (!plan.isFinished()) continue;
        if (!planId.isEmpty() && plan.planId != planId) continue;
        const PlanReport r = PlanReport::build(plan, history.runs);
        CycleMetrics c;
        c.planRunId = plan.id;
        c.planId = plan.planId;
        c.name = plan.name;
        c.startedAt = plan.startedAt;
        c.finishedAt = plan.finishedAt;
        c.total = r.total();
        c.executed = r.executed;
        c.passed = r.passed;
        c.failed = r.failed;
        c.blocked = r.blocked;
        c.durationSecs = r.durationSecs;
        out.append(c);
    }
    std::stable_sort(out.begin(), out.end(), [](const CycleMetrics& a, const CycleMetrics& b) { return a.finishedAt < b.finishedAt; });
    return out;
}

std::optional<int> trend(const QList<CycleMetrics>& cycles) {
    if (cycles.size() < 2) return std::nullopt;
    return cycles.last().successRate() - cycles[cycles.size() - 2].successRate();
}

} // namespace qaflow::metrics
