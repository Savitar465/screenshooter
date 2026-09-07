#include "BugReportService.h"

#include "application/RunController.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"

namespace qaflow {

BugReportService::BugReportService(std::shared_ptr<IIssueTracker> tracker, TestCaseStore& cases, RunController& run,
                                   SettingsStore& settings, QObject* parent)
    : QObject(parent), m_tracker(std::move(tracker)), m_cases(cases), m_run(run), m_settings(settings) {}

BugReport BugReportService::draftFromCurrentContext() const {
    BugReport b;
    const TestCase* c = m_cases.selected();
    if (!c) return b;

    b.linkedCaseId = c->id;
    QStringList lines;
    for (int i = 0; i < c->steps.size(); ++i) lines << QStringLiteral("%1. %2").arg(i + 1).arg(c->steps[i].action);
    b.stepsToReproduce = lines.join(QLatin1Char('\n'));

    const RunState& r = m_run.state();
    const int failIdx = r.caseId == c->id ? r.firstFailIndex() : -1;
    if (failIdx >= 0 && failIdx < c->steps.size()) {
        const TestStep& failed = c->steps[failIdx];
        b.title = QStringLiteral("[%1] Falla en paso %2: %3").arg(c->suite).arg(failIdx + 1).arg(failed.action);
        b.expected = failed.expected;
        b.actual = r.results[failIdx].note;
    }
    for (const auto& s : c->shots) b.attachmentPaths << s.path;
    b.linkedStoryKey = c->jiraKey;
    return b;
}

void BugReportService::submit(const BugReport& bug, std::function<void(const IssueResult&)> done) {
    if (!m_tracker) { done(IssueResult{false, {}, {}, QStringLiteral("Integración no disponible")}); return; }
    m_tracker->createIssue(m_settings.jira(), bug, std::move(done));
}

void BugReportService::testConnection(std::function<void(const ConnectionResult&)> done) {
    if (!m_tracker) { done(ConnectionResult{false, {}, QStringLiteral("Integración no disponible")}); return; }
    m_tracker->testConnection(m_settings.jira(), std::move(done));
}

} // namespace qaflow
