#include "RunController.h"

#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"

#include <algorithm>

namespace qaflow {

RunController::RunController(TestCaseStore& store, RunHistoryStore& history, QObject* parent)
    : QObject(parent), m_store(store), m_history(history) {}

int RunController::totalSteps() const {
    const auto* c = m_store.find(m_run.caseId);
    return c ? c->steps.size() : 0;
}

void RunController::begin(const QString& caseId) {
    m_run = RunState{};
    m_run.caseId = caseId;
    m_run.startedAt = QDateTime::currentDateTime();
    m_run.finished = totalSteps() == 0;
    m_store.select(caseId);
}

void RunController::start(const QString& caseId) {
    commitIfFinished();
    closePlan();
    m_queue.clear();
    begin(caseId);
    emit runChanged();
}

void RunController::startSequence(const QStringList& caseIds, const QString& planName) {
    if (caseIds.isEmpty()) return;
    commitIfFinished();
    closePlan();
    m_planRunId = m_history.startPlan(planName, caseIds);
    m_queue = caseIds.mid(1);
    begin(caseIds.first());
    emit runChanged();
}

void RunController::restart() {
    commitIfFinished();
    m_run.idx = 0;
    m_run.results.clear();
    m_run.note.clear();
    m_run.startedAt = QDateTime::currentDateTime();
    m_run.finished = totalSteps() == 0;
    emit runChanged();
}

void RunController::setNote(const QString& note) { m_run.note = note; }

void RunController::mark(StepResult result) {
    if (m_run.caseId.isEmpty() || m_run.finished) return;
    const int total = totalSteps();
    m_run.results.append(StepRecord{result, m_run.note});
    m_run.note.clear();
    m_run.finished = m_run.results.size() >= total || result == StepResult::Block;
    m_run.idx = std::min(static_cast<int>(m_run.results.size()), total - 1);
    emit runChanged();
}

void RunController::commitIfFinished() {
    if (!m_run.finished || m_run.caseId.isEmpty()) return;
    const TestCase* c = m_store.find(m_run.caseId);
    if (!c) return;

    RunRecord rec;
    rec.caseId = c->id;
    rec.caseTitle = c->title;
    rec.suite = c->suite;
    rec.planRunId = m_planRunId;
    rec.startedAt = m_run.startedAt;
    rec.finishedAt = QDateTime::currentDateTime();
    rec.verdict = m_run.verdict();
    rec.plannedSteps = c->steps.size();
    for (int i = 0; i < m_run.results.size() && i < c->steps.size(); ++i)
        rec.steps.append(RunRecordStep{c->steps[i].action, c->steps[i].expected, m_run.results[i].result, m_run.results[i].note});
    m_history.addRun(rec);

    switch (rec.verdict) {
        case Verdict::Superado: m_store.recordOutcome(c->id, RunOutcome::Passed); break;
        case Verdict::Fallido: m_store.recordOutcome(c->id, RunOutcome::Failed); break;
        case Verdict::Bloqueado: m_store.recordOutcome(c->id, RunOutcome::Blocked); break;
    }
    // Un caso sin pasos "termina" al arrancar; no vuelve a archivarse.
    m_run.finished = false;
    m_run.results.clear();
}

void RunController::closePlan() {
    if (m_planRunId.isEmpty()) return;
    const QString id = m_planRunId;
    m_planRunId.clear();
    m_queue.clear();
    m_history.finishPlan(id);
    emit planCompleted(id);
}

bool RunController::finish() {
    if (m_run.caseId.isEmpty()) return false;
    commitIfFinished();
    if (!m_queue.isEmpty()) {
        begin(m_queue.takeFirst());
        emit runChanged();
        return true;
    }
    m_run = RunState{};
    closePlan();
    emit runChanged();
    return false;
}

void RunController::abandon() {
    commitIfFinished();
    m_run = RunState{};
    closePlan();
    emit runChanged();
}

} // namespace qaflow
