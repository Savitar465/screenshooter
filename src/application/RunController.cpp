#include "RunController.h"

#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"

#include <algorithm>

namespace qaflow {

RunController::RunController(TestCaseStore& store, RunHistoryStore& history,
                             std::shared_ptr<IRunSessionRepository> session, QObject* parent)
    : QObject(parent), m_store(store), m_history(history), m_session(std::move(session)) {
    // Las notas llegan tecla a tecla; agrupamos las escrituras a disco.
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(300);
    connect(&m_saveTimer, &QTimer::timeout, this, &RunController::persistSession);
}

RunController::~RunController() {
    if (m_saveTimer.isActive()) persistSession();
}

void RunController::load() {
    if (!m_session) return;
    auto saved = m_session->loadSession();
    if (!saved || saved->run.caseId.isEmpty() || !m_store.find(saved->run.caseId)) {
        if (saved) m_session->clearSession();
        return;
    }
    m_run = saved->run;
    m_queue = saved->queue;
    m_planRunId = saved->planRunId;
    // Sólo cuentan los pasos que siguen existiendo si el caso se editó entre sesiones.
    const int total = totalSteps();
    if (m_run.results.size() > total) m_run.results = m_run.results.mid(0, total);
    recomputeFinished();
    // El tiempo con la aplicación cerrada no cuenta: el paso actual vuelve a arrancar ahora.
    m_run.stepStartedAt = QDateTime::currentDateTime();
    m_store.select(m_run.caseId);
    emit runChanged();
}

int RunController::totalSteps() const {
    const auto* c = m_store.find(m_run.caseId);
    return c ? c->steps.size() : 0;
}

void RunController::startStepClock() {
    m_run.stepStartedAt = QDateTime::currentDateTime();
    m_run.stepElapsedSecs = 0;
}

void RunController::recomputeFinished() {
    const int total = totalSteps();
    m_run.finished = m_run.results.size() >= total || (!m_run.results.isEmpty() && m_run.results.last().result == StepResult::Block);
    m_run.idx = total == 0 ? 0 : std::min(static_cast<int>(m_run.results.size()), total - 1);
}

void RunController::begin(const QString& caseId) {
    m_run = RunState{};
    m_run.caseId = caseId;
    m_run.startedAt = QDateTime::currentDateTime();
    m_run.finished = totalSteps() == 0;
    startStepClock();
    m_store.select(caseId);
}

void RunController::start(const QString& caseId) {
    commitIfFinished();
    closePlan();
    m_queue.clear();
    begin(caseId);
    changed();
}

void RunController::startSequence(const QStringList& caseIds, const QString& planName, const QString& planId) {
    if (caseIds.isEmpty()) return;
    commitIfFinished();
    closePlan();
    m_planRunId = m_history.startPlan(planName, caseIds, planId);
    m_queue = caseIds.mid(1);
    begin(caseIds.first());
    changed();
}

void RunController::restart() {
    commitIfFinished();
    m_run.idx = 0;
    m_run.results.clear();
    m_run.note.clear();
    m_run.startedAt = QDateTime::currentDateTime();
    m_run.finished = totalSteps() == 0;
    startStepClock();
    changed();
}

void RunController::setNote(const QString& note) {
    if (m_run.note == note) return;
    m_run.note = note;
    m_saveTimer.start();
}

void RunController::mark(StepResult result) {
    if (m_run.caseId.isEmpty() || m_run.finished) return;
    m_run.results.append(StepRecord{result, m_run.note, m_run.currentStepSecs()});
    m_run.note.clear();
    recomputeFinished();
    startStepClock();
    changed();
}

void RunController::back() {
    if (m_run.caseId.isEmpty() || m_run.results.isEmpty()) return;
    const StepRecord last = m_run.results.takeLast();
    m_run.note = last.note;
    m_run.finished = false;
    m_run.idx = m_run.results.size();
    // El paso vuelve a estar en pantalla: retoma su cronómetro donde se quedó.
    m_run.stepStartedAt = QDateTime::currentDateTime();
    m_run.stepElapsedSecs = last.durationSecs;
    changed();
}

void RunController::setResult(int index, StepResult result) {
    if (m_run.caseId.isEmpty() || index < 0 || index >= m_run.results.size()) return;
    if (m_run.results[index].result == result) return;
    m_run.results[index].result = result;
    const bool wasFinished = m_run.finished;
    recomputeFinished();
    if (wasFinished && !m_run.finished) startStepClock(); // se quitó un bloqueo: continúa
    changed();
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
    for (int i = 0; i < m_run.results.size() && i < c->steps.size(); ++i) {
        const StepRecord& r = m_run.results[i];
        rec.steps.append(RunRecordStep{c->steps[i].action, c->steps[i].expected, r.result, r.note, r.durationSecs});
        rec.durationSecs += r.durationSecs;
    }
    const RunRecord saved = m_history.addRun(rec);
    // La evidencia capturada durante la ejecución pasa a ser suya: desde aquí se enseña y se
    // publica con ella, no con el caso.
    m_store.sealShots(c->id, saved.id);

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
        changed();
        return true;
    }
    m_run = RunState{};
    closePlan();
    changed();
    return false;
}

void RunController::abandon() {
    commitIfFinished();
    m_run = RunState{};
    closePlan();
    changed();
}

void RunController::changed() {
    emit runChanged();
    m_saveTimer.stop();
    persistSession();
}

bool RunController::persistSession() {
    m_saveTimer.stop();
    if (!m_session) return false;
    if (m_run.caseId.isEmpty()) { m_session->clearSession(); return true; }
    RunSession s{m_run, m_queue, m_planRunId};
    // Lo transcurrido en este paso se consolida para que al restaurar siga desde aquí.
    s.run.stepElapsedSecs = m_run.currentStepSecs();
    s.run.stepStartedAt = QDateTime();
    if (m_session->saveSession(s)) return true;
    emit saveFailed(tr("la ejecución en curso"));
    return false;
}

} // namespace qaflow
