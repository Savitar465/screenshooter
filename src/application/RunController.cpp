#include "RunController.h"

#include "application/TestCaseStore.h"

#include <algorithm>

namespace qaflow {

RunController::RunController(TestCaseStore& store, QObject* parent) : QObject(parent), m_store(store) {}

int RunController::totalSteps() const {
    const auto* c = m_store.find(m_run.caseId);
    return c ? c->steps.size() : 0;
}

void RunController::start(const QString& caseId) {
    m_queue.clear();
    m_run = RunState{};
    m_run.caseId = caseId;
    m_run.finished = totalSteps() == 0;
    m_store.select(caseId);
    emit runChanged();
}

void RunController::startSequence(const QStringList& caseIds) {
    if (caseIds.isEmpty()) return;
    start(caseIds.first());
    m_queue = caseIds.mid(1);
    emit runChanged();
}

void RunController::restart() {
    m_run.idx = 0;
    m_run.results.clear();
    m_run.note.clear();
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

void RunController::recordVerdict() {
    if (!m_run.finished || m_run.caseId.isEmpty()) return;
    const Verdict v = m_run.verdict();
    if (v == Verdict::Superado) m_store.recordOutcome(m_run.caseId, RunOutcome::Passed);
    else if (v == Verdict::Fallido) m_store.recordOutcome(m_run.caseId, RunOutcome::Failed);
}

bool RunController::finish() {
    if (m_run.caseId.isEmpty()) return false;
    recordVerdict();
    if (!m_queue.isEmpty()) {
        const QString next = m_queue.takeFirst();
        m_run = RunState{};
        m_run.caseId = next;
        m_run.finished = totalSteps() == 0;
        m_store.select(next);
        emit runChanged();
        return true;
    }
    m_run = RunState{};
    emit runChanged();
    return false;
}

void RunController::abandon() {
    m_queue.clear();
    m_run = RunState{};
    emit runChanged();
}

} // namespace qaflow
