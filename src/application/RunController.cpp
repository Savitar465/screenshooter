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
    // La lista siempre tiene un registro por paso: se recorta si el caso perdió pasos entre
    // sesiones y se completa con pendientes si ganó alguno.
    m_run.results.resize(totalSteps());
    recomputeFinished();
    // El tiempo con la aplicación cerrada no cuenta: el paso actual vuelve a arrancar ahora.
    // (Las sesiones antiguas sólo guardaban los pasos ejecutados: el reloj llega aparte.)
    if (m_run.idx < m_run.results.size())
        m_run.results[m_run.idx].durationSecs = std::max(m_run.results[m_run.idx].durationSecs, m_run.stepElapsedSecs);
    m_run.stepElapsedSecs = m_run.idx < m_run.results.size() ? m_run.results[m_run.idx].durationSecs : 0;
    m_run.stepStartedAt = QDateTime::currentDateTime();
    m_store.select(m_run.caseId);
    emit runChanged();
}

int RunController::totalSteps() const {
    const auto* c = m_store.find(m_run.caseId);
    return c ? c->steps.size() : 0;
}

void RunController::holdStep() {
    if (m_run.idx < 0 || m_run.idx >= m_run.results.size()) return;
    m_run.results[m_run.idx].durationSecs = m_run.currentStepSecs();
    m_run.results[m_run.idx].note = m_run.note;
}

void RunController::enterStep(int index) {
    holdStep();
    m_run.idx = index;
    const bool valid = index >= 0 && index < m_run.results.size();
    m_run.note = valid ? m_run.results[index].note : QString();
    m_run.stepElapsedSecs = valid ? m_run.results[index].durationSecs : 0;
    m_run.stepStartedAt = QDateTime::currentDateTime();
}

void RunController::recomputeFinished() {
    const int total = totalSteps();
    m_run.finished = total == 0 || m_run.allMarked();
    m_run.idx = total == 0 ? 0 : std::clamp(m_run.idx, 0, total - 1);
}

void RunController::begin(const QString& caseId) {
    m_run = RunState{};
    m_run.caseId = caseId;
    m_run.startedAt = QDateTime::currentDateTime();
    m_run.results = QList<StepRecord>(totalSteps());
    m_run.finished = m_run.results.isEmpty();
    m_run.stepStartedAt = QDateTime::currentDateTime();
    m_store.select(caseId);
}

void RunController::start(const QString& caseId) {
    commitRun(false);
    closePlan();
    m_queue.clear();
    begin(caseId);
    changed();
}

void RunController::startSequence(const QStringList& caseIds, const QString& planName, const QString& planId) {
    if (caseIds.isEmpty()) return;
    commitRun(false);
    closePlan();
    m_planRunId = m_history.startPlan(planName, caseIds, planId);
    m_queue = caseIds.mid(1);
    begin(caseIds.first());
    changed();
    emit planStarted(m_planRunId, planId);
}

void RunController::restart() {
    commitRun(false);
    begin(QString(m_run.caseId));   // la cola del plan no se toca: se repite este caso, no el plan
    changed();
}

void RunController::setNote(const QString& note) {
    if (m_run.note == note) return;
    m_run.note = note;
    if (m_run.idx >= 0 && m_run.idx < m_run.results.size()) m_run.results[m_run.idx].note = note;
    m_saveTimer.start();
}

void RunController::mark(StepResult result) {
    if (m_run.caseId.isEmpty() || m_run.idx < 0 || m_run.idx >= m_run.results.size()) return;
    const int current = m_run.idx;
    StepRecord& rec = m_run.results[current];
    rec.result = result;
    rec.note = m_run.note;
    rec.durationSecs = m_run.currentStepSecs();
    rec.marked = true;
    // Avanza al siguiente paso pendiente; si no queda ninguno, se queda donde está y la
    // ejecución pasa a "terminada". Un fallo o un bloqueo ya no la cortan: se sigue navegando.
    const int pending = m_run.nextPending(current + 1);
    if (pending >= 0) enterStep(pending);
    recomputeFinished();
    changed();
}

void RunController::goTo(int index) {
    if (m_run.caseId.isEmpty() || m_run.results.isEmpty()) return;
    const int target = std::clamp(index, 0, static_cast<int>(m_run.results.size()) - 1);
    if (target == m_run.idx && !m_run.finished) return;
    // Volver a un paso reabre una ejecución que ya estaba terminada: sus veredictos siguen ahí y
    // se pueden cambiar sin perder nada.
    m_run.finished = false;
    enterStep(target);
    changed();
}

void RunController::back() { goTo(m_run.finished ? m_run.idx : m_run.idx - 1); }

void RunController::next() { goTo(m_run.idx + 1); }

void RunController::setResult(int index, StepResult result) {
    if (m_run.caseId.isEmpty() || index < 0 || index >= m_run.results.size()) return;
    if (m_run.results[index].marked && m_run.results[index].result == result) return;
    m_run.results[index].result = result;
    m_run.results[index].marked = true;
    recomputeFinished();
    changed();
}

void RunController::commitRun(bool evenIfPending) {
    if (m_run.caseId.isEmpty()) return;
    const int last = m_run.lastMarkedIndex();
    // Terminada se archiva siempre (un caso sin pasos también); a medias, sólo si se marcó algo.
    if (!m_run.finished && !(evenIfPending && last >= 0)) return;
    const TestCase* c = m_store.find(m_run.caseId);
    if (!c) return;
    holdStep();

    RunRecord rec;
    rec.caseId = c->id;
    rec.caseTitle = c->title;
    rec.suite = c->suite;
    rec.planRunId = m_planRunId;
    rec.startedAt = m_run.startedAt;
    rec.finishedAt = QDateTime::currentDateTime();
    rec.verdict = m_run.verdict();
    rec.plannedSteps = c->steps.size();
    // Se archiva hasta el último paso con veredicto; los huecos que se dejaron sin marcar quedan
    // como N/A, que es lo que son: pasos que no llegaron a ejecutarse.
    for (int i = 0; i <= last && i < c->steps.size(); ++i) {
        const StepRecord& r = m_run.results[i];
        rec.steps.append(RunRecordStep{c->steps[i].action, c->steps[i].expected,
                                       r.marked ? r.result : StepResult::Skip, r.note, r.durationSecs});
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
    m_run.results = QList<StepRecord>(m_run.results.size());
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
    commitRun(true);
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
    commitRun(false);
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
    if (s.run.idx >= 0 && s.run.idx < s.run.results.size()) {
        s.run.results[s.run.idx].durationSecs = s.run.stepElapsedSecs;
        s.run.results[s.run.idx].note = s.run.note;
    }
    s.run.stepStartedAt = QDateTime();
    if (m_session->saveSession(s)) return true;
    emit saveFailed(tr("la ejecución en curso"));
    return false;
}

} // namespace qaflow
