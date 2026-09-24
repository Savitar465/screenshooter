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
    m_continuesRunId = saved->continuesRunId;
    for (const auto& p : saved->parked)
        if (m_store.find(p.run.caseId) && m_queue.contains(p.run.caseId)) m_parked.insert(p.run.caseId, p);
    // La lista siempre tiene un registro por paso: se recorta si el caso perdió pasos entre
    // sesiones y se completa con pendientes si ganó alguno.
    m_run.results.resize(totalSteps());
    recomputeFinished();
    // El tiempo con la aplicación cerrada no cuenta: el paso actual vuelve a arrancar ahora.
    // (Las sesiones antiguas sólo guardaban los pasos ejecutados: el reloj llega aparte.)
    if (m_run.idx < m_run.results.size())
        m_run.results[m_run.idx].durationSecs = std::max(m_run.results[m_run.idx].durationSecs, m_run.stepElapsedSecs);
    m_run.stepElapsedSecs = m_run.idx < m_run.results.size() ? m_run.results[m_run.idx].durationSecs : 0;
    if (m_run.finished) m_run.paused = false;
    startStepClock();   // una ejecución que se cerró en pausa sigue en pausa
    m_store.select(m_run.caseId);
    emit runChanged();
}

QStringList RunController::planCases() const {
    const PlanRun* plan = m_planRunId.isEmpty() ? nullptr : m_history.findPlan(m_planRunId);
    return plan ? plan->caseIds : QStringList{};
}

const RunState* RunController::parkedRun(const QString& caseId) const {
    const auto it = m_parked.constFind(caseId);
    return it == m_parked.constEnd() ? nullptr : &it->run;
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

void RunController::startStepClock() {
    m_run.stepStartedAt = m_run.paused ? QDateTime() : QDateTime::currentDateTime();
}

void RunController::enterStep(int index) {
    holdStep();
    m_run.idx = index;
    const bool valid = index >= 0 && index < m_run.results.size();
    m_run.note = valid ? m_run.results[index].note : QString();
    m_run.stepElapsedSecs = valid ? m_run.results[index].durationSecs : 0;
    startStepClock();
}

void RunController::recomputeFinished() {
    const int total = totalSteps();
    m_run.finished = total == 0 || m_run.allMarked();
    m_run.idx = total == 0 ? 0 : std::clamp(m_run.idx, 0, total - 1);
}

void RunController::begin(const QString& caseId) {
    m_run = RunState{};
    m_run.caseId = caseId;
    // El id se pide ahora, no al archivar: los bugs que se reporten mientras corre la ejecución se
    // enlazan con ella, y para eso tiene que tener nombre desde el principio.
    m_run.runId = m_history.reserveRunId();
    m_run.startedAt = QDateTime::currentDateTime();
    m_run.results = QList<StepRecord>(totalSteps());
    m_run.finished = m_run.results.isEmpty();
    m_run.stepStartedAt = QDateTime::currentDateTime();
    m_continuesRunId.clear();
    // Si este caso viene de una continuación, se retoma donde se rompió en vez de empezar de cero. La
    // entrada se consume: repetir el caso desde la pantalla («Repetir») lo ejecuta entero otra vez.
    if (const auto it = m_resume.constFind(caseId); it != m_resume.constEnd()) {
        const RunRecord previous = *it;
        m_resume.erase(m_resume.find(caseId));
        if (const TestCase* c = m_store.find(caseId)) resumeFrom(previous, *c);
    }
    m_store.select(caseId);
}

void RunController::enterCase(const QString& caseId) {
    const auto it = m_parked.constFind(caseId);
    if (it == m_parked.constEnd()) {
        begin(caseId);
        return;
    }
    m_run = it->run;
    m_run.paused = false;   // volver a un caso es ponerse a probarlo
    m_continuesRunId = it->continuesRunId;
    m_parked.erase(m_parked.find(caseId));
    // Como al restaurar la sesión: el caso pudo cambiar de pasos mientras estaba aparcado, y el
    // tiempo que pasó fuera no cuenta.
    m_run.results.resize(totalSteps());
    recomputeFinished();
    m_run.stepElapsedSecs = m_run.idx < m_run.results.size() ? m_run.results[m_run.idx].durationSecs : 0;
    m_run.stepStartedAt = QDateTime::currentDateTime();
    m_run.note = m_run.idx < m_run.results.size() ? m_run.results[m_run.idx].note : QString();
    m_store.select(caseId);
}

void RunController::resumeFrom(const RunRecord& previous, const TestCase& c) {
    const int broken = previous.brokenStepIndex();
    if (broken < 0) return;
    m_continuesRunId = previous.id;
    // Lo anterior al paso roto se hereda mientras el caso no haya cambiado: un paso que hoy dice otra
    // cosa no se puede dar por superado con lo que se probó entonces.
    for (int i = 0; i < broken && i < m_run.results.size() && i < previous.steps.size(); ++i) {
        if (c.steps[i].action != previous.steps[i].action || c.steps[i].data != previous.steps[i].data
            || c.steps[i].expected != previous.steps[i].expected) break;
        m_run.results[i].result = previous.steps[i].result;
        m_run.results[i].note = previous.steps[i].note;
        m_run.results[i].marked = true;   // el cronómetro empieza de nuevo: el tiempo es el de ahora
        m_run.results[i].inherited = true;
    }
    // Y se arranca en el primer paso pendiente, que es el que se rompió (o el primero del caso si no
    // se pudo heredar nada). El reloj de ese paso empieza ahora; no se pasa por `enterStep` porque
    // guardaría encima del paso heredado la nota vacía del estado recién creado.
    const int pending = m_run.nextPending(0);
    m_run.idx = std::clamp(pending < 0 ? broken : pending, 0, std::max(0, static_cast<int>(m_run.results.size()) - 1));
    m_run.note = m_run.results[m_run.idx].note;
    m_run.stepElapsedSecs = 0;
    m_run.stepStartedAt = QDateTime::currentDateTime();
    recomputeFinished();
}

void RunController::start(const QString& caseId) {
    commitRun(false);
    closePlan();
    m_queue.clear();
    m_resume.clear();
    begin(caseId);
    changed();
}

void RunController::startSequence(const QStringList& caseIds, const QString& planName, const QString& planId,
                                  const QString& environment) {
    if (caseIds.isEmpty()) return;
    commitRun(false);
    closePlan();
    m_resume.clear();   // un ciclo normal ejecuta sus casos enteros
    m_planRunId = m_history.startPlan(planName, caseIds, planId, environment);
    m_queue = caseIds.mid(1);
    begin(caseIds.first());
    changed();
    emit planStarted(m_planRunId, planId);
}

bool RunController::continueCycle(const QString& planRunId, const QString& environment) {
    const PlanRun* cycle = m_history.findPlan(planRunId);
    if (!cycle || !cycle->isFinished()) return false;
    const PlanReport report = m_history.report(planRunId);
    // Sólo lo que quedó roto, en el orden del plan y saltando los casos que ya no están en el catálogo.
    QStringList caseIds;
    QHash<QString, RunRecord> resume;
    for (const auto& row : report.rows) {
        if (!row.executed || !row.run.isBroken() || !m_store.find(row.caseId)) continue;
        caseIds << row.caseId;
        resume.insert(row.caseId, row.run);
    }
    if (caseIds.isEmpty()) return false;
    // Lo que hace falta del ciclo que se continúa se copia ahora: apuntar un ciclo nuevo reubica la
    // lista del historial y el puntero deja de valer. Con él se iba el plan del aviso de arranque, y
    // sin plan nadie abría la revisión siguiente ni sellaba con ella el ciclo de la continuación.
    const QString name = cycle->name, planId = cycle->planId, previous = cycle->id;

    commitRun(false);
    closePlan();
    m_resume = resume;
    m_planRunId = m_history.startPlan(name, caseIds, planId, environment, previous);
    m_queue = caseIds.mid(1);
    begin(caseIds.first());
    changed();
    emit planStarted(m_planRunId, planId);
    return true;
}

void RunController::restart() {
    commitRun(false);
    begin(QString(m_run.caseId));   // la cola del plan no se toca: se repite este caso, no el plan
    changed();
}

void RunController::pause() {
    if (!isRunning() || m_run.paused) return;
    // Lo que lleva el paso se consolida y el reloj se para: la pausa no cuenta.
    m_run.stepElapsedSecs = m_run.currentStepSecs();
    m_run.paused = true;
    startStepClock();
    changed();
}

void RunController::resume() {
    if (!m_run.paused) return;
    m_run.paused = false;
    startStepClock();
    changed();
}

void RunController::setNote(const QString& note) {
    if (m_run.note == note) return;
    m_run.note = note;
    if (m_run.idx >= 0 && m_run.idx < m_run.results.size()) m_run.results[m_run.idx].note = note;
    m_saveTimer.start();
}

void RunController::mark(StepResult result) {
    if (m_run.caseId.isEmpty() || m_run.paused || m_run.idx < 0 || m_run.idx >= m_run.results.size()) return;
    const int current = m_run.idx;
    StepRecord& rec = m_run.results[current];
    rec.result = result;
    rec.note = m_run.note;
    rec.durationSecs = m_run.currentStepSecs();
    rec.marked = true;
    rec.inherited = false;   // volver a darle veredicto lo hace de esta ejecución
    // Avanza al siguiente paso pendiente; si no queda ninguno, se queda donde está y la
    // ejecución pasa a "terminada". Un fallo o un bloqueo ya no la cortan: se sigue navegando.
    const int pending = m_run.nextPending(current + 1);
    if (pending >= 0) enterStep(pending);
    recomputeFinished();
    changed();
}

void RunController::goTo(int index) {
    if (m_run.caseId.isEmpty() || m_run.paused || m_run.results.isEmpty()) return;
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

bool RunController::goToCase(const QString& caseId) {
    if (m_planRunId.isEmpty() || m_run.caseId.isEmpty() || !m_queue.contains(caseId)) return false;
    holdStep();
    m_parked.insert(m_run.caseId, ParkedRun{m_run, m_continuesRunId});
    // El que se deja vuelve a la cola en su sitio del plan, para que «Siguiente caso» siga el orden.
    const QStringList order = planCases();
    m_queue.removeAll(caseId);
    m_queue << m_run.caseId;
    std::stable_sort(m_queue.begin(), m_queue.end(), [&order](const QString& a, const QString& b) {
        return order.indexOf(a) < order.indexOf(b);
    });
    enterCase(caseId);
    changed();
    return true;
}

void RunController::setResult(int index, StepResult result) {
    if (m_run.caseId.isEmpty() || m_run.paused || index < 0 || index >= m_run.results.size()) return;
    if (m_run.results[index].marked && m_run.results[index].result == result) return;
    m_run.results[index].result = result;
    m_run.results[index].marked = true;
    m_run.results[index].inherited = false;
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
    rec.id = m_run.runId;   // el que se reservó al arrancar, con el que la conocen sus bugs
    rec.caseId = c->id;
    rec.caseTitle = c->title;
    rec.suite = c->suite;
    rec.planRunId = m_planRunId;
    rec.continuesRunId = m_continuesRunId;
    rec.startedAt = m_run.startedAt;
    rec.finishedAt = QDateTime::currentDateTime();
    rec.verdict = m_run.verdict();
    rec.plannedSteps = c->steps.size();
    // Se archiva hasta el último paso con veredicto; los huecos que se dejaron sin marcar quedan
    // como N/A, que es lo que son: pasos que no llegaron a ejecutarse.
    for (int i = 0; i <= last && i < c->steps.size(); ++i) {
        const StepRecord& r = m_run.results[i];
        rec.steps.append(RunRecordStep{c->steps[i].action, c->steps[i].data, c->steps[i].expected,
                                       r.marked ? r.result : StepResult::Skip, r.note, r.durationSecs,
                                       r.marked && r.inherited && !m_continuesRunId.isEmpty()});
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
    m_continuesRunId.clear();
}

void RunController::closePlan() {
    if (m_planRunId.isEmpty()) return;
    // Los casos aparcados con todos sus pasos marcados se archivan como el que está en pantalla; los
    // que quedaron a medias, igual que él al abandonar, no.
    const RunState current = m_run;
    const QString continues = m_continuesRunId;
    for (const auto& p : std::as_const(m_parked)) {
        m_run = p.run;
        m_continuesRunId = p.continuesRunId;
        commitRun(false);
    }
    m_parked.clear();
    m_run = current;
    m_continuesRunId = continues;
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
        enterCase(m_queue.takeFirst());
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
    RunSession s{m_run, m_queue, m_planRunId, m_continuesRunId, m_parked.values()};
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
