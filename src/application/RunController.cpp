#include "RunController.h"

#include "application/RunHistoryStore.h"
#include "application/TestCaseStore.h"

#include <QSet>

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
    restore(*saved);
    // Un ciclo en pausa no ocupa la ejecución: si se cerró así (una sesión de antes de que se
    // aparcaran), se aparca ahora y queda libre.
    const PlanRun* plan = m_planRunId.isEmpty() ? nullptr : m_history.findPlan(m_planRunId);
    if (plan && (plan->isPaused() || m_run.paused)) {
        m_run.paused = false;
        suspendCycle();
        return;
    }
    startStepClock();   // un caso suelto que se cerró en pausa sigue en pausa
    m_store.select(m_run.caseId);
    emit runChanged();
}

RunSession RunController::snapshot() const {
    RunSession s{m_run, m_queue, m_planRunId, m_continuesRunId, m_parked.values(), {}, {}};
    for (auto it = m_resume.cbegin(); it != m_resume.cend(); ++it) s.resume.insert(it.key(), it->id);
    for (auto it = m_review.cbegin(); it != m_review.cend(); ++it) s.review.insert(it.key(), it->id);
    // Lo transcurrido en este paso se consolida para que al restaurar siga desde aquí.
    s.run.stepElapsedSecs = m_run.currentStepSecs();
    if (s.run.idx >= 0 && s.run.idx < s.run.results.size()) {
        s.run.results[s.run.idx].durationSecs = s.run.stepElapsedSecs;
        s.run.results[s.run.idx].note = s.run.note;
    }
    s.run.stepStartedAt = QDateTime();
    return s;
}

void RunController::restore(const RunSession& saved) {
    m_run = saved.run;
    m_queue = saved.queue;
    m_planRunId = saved.planRunId;
    m_continuesRunId = saved.continuesRunId;
    m_parked.clear();
    for (const auto& p : saved.parked)
        if (m_store.find(p.run.caseId) && m_queue.contains(p.run.caseId)) m_parked.insert(p.run.caseId, p);
    // Los casos de la cola que se retoman (o se revisan) desde lo archivado: sin ellos, al llegar se
    // ejecutarían enteros.
    m_resume.clear();
    m_review.clear();
    for (auto it = saved.resume.cbegin(); it != saved.resume.cend(); ++it)
        if (const RunRecord* r = m_history.findRun(it.value()); r && m_queue.contains(it.key())) m_resume.insert(it.key(), *r);
    for (auto it = saved.review.cbegin(); it != saved.review.cend(); ++it)
        if (const RunRecord* r = m_history.findRun(it.value()); r && m_queue.contains(it.key())) m_review.insert(it.key(), *r);
    // La lista siempre tiene un registro por paso: se recorta si el caso perdió pasos entre
    // sesiones y se completa con pendientes si ganó alguno.
    m_run.results.resize(totalSteps());
    recomputeFinished();
    // El tiempo fuera (con la aplicación cerrada o el ciclo aparcado) no cuenta: el paso actual vuelve
    // a arrancar ahora. (Las sesiones antiguas sólo guardaban los pasos ejecutados: el reloj llega aparte.)
    if (m_run.idx < m_run.results.size())
        m_run.results[m_run.idx].durationSecs = std::max(m_run.results[m_run.idx].durationSecs, m_run.stepElapsedSecs);
    m_run.stepElapsedSecs = m_run.idx < m_run.results.size() ? m_run.results[m_run.idx].durationSecs : 0;
}

bool RunController::suspendCycle() {
    if (m_planRunId.isEmpty() || m_run.caseId.isEmpty() || !m_session) return false;
    holdStep();
    const RunSession s = snapshot();
    if (!m_session->saveSuspended(m_planRunId, s)) {
        emit saveFailed(tr("el ciclo en pausa"));
        return false;
    }
    const QString id = m_planRunId;
    m_history.setPlanPaused(id, true);
    // La ejecución queda libre: el ciclo sigue abierto en el historial, con su sesión aparcada.
    m_run = RunState{};
    m_queue.clear();
    m_parked.clear();
    m_resume.clear();
    m_review.clear();
    m_planRunId.clear();
    m_continuesRunId.clear();
    changed();
    emit cycleSuspended(id);
    return true;
}

bool RunController::isSuspended(const QString& planRunId) const {
    const PlanRun* plan = m_history.findPlan(planRunId);
    return plan && plan->isPaused() && planRunId != m_planRunId;
}

bool RunController::resumeCycle(const QString& planRunId) {
    if (!m_run.caseId.isEmpty() || !m_session || !isSuspended(planRunId)) return false;
    const auto saved = m_session->loadSuspended(planRunId);
    if (!saved || saved->run.caseId.isEmpty() || !m_store.find(saved->run.caseId)) return false;
    restore(*saved);
    m_planRunId = planRunId;
    m_run.paused = false;
    m_session->clearSuspended(planRunId);
    m_history.setPlanPaused(planRunId, false);
    startStepClock();
    m_store.select(m_run.caseId);
    changed();
    return true;
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
    m_run.runId = reserveRunId();
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
    } else if (const auto rv = m_review.constFind(caseId); rv != m_review.constEnd()) {
        const RunRecord archived = *rv;
        m_review.erase(m_review.find(caseId));
        if (const TestCase* c = m_store.find(caseId)) reviewFrom(archived, *c);
    }
    m_store.select(caseId);
}

QString RunController::reserveRunId() const {
    QStringList held;
    for (const auto& p : m_parked) held << p.run.runId;
    return m_history.reserveRunId(held);
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
    m_review.clear();
    begin(caseId);
    changed();
}

void RunController::startSequence(const QStringList& caseIds, const QString& planName, const QString& planId,
                                  const QString& environment, const QStringList& excluded, const QStringList& added) {
    if (caseIds.isEmpty()) return;
    commitRun(false);
    closePlan();
    m_resume.clear();   // un ciclo normal ejecuta sus casos enteros
    m_review.clear();
    m_planRunId = m_history.startPlan(planName, caseIds, planId, environment, excluded, added);
    m_queue = caseIds.mid(1);
    begin(caseIds.first());
    changed();
    emit planStarted(m_planRunId, planId);
}

bool RunController::continueCycle(const QString& planRunId, const QString& environment, const QStringList& planCases) {
    const PlanRun* cycle = m_history.findPlan(planRunId);
    if (!cycle || !cycle->isFinished()) return false;
    const PlanReport report = m_history.report(planRunId);
    // Lo roto (que se retoma en su paso), lo que quedó sin ejecutar y lo que el plan ganó después, en el
    // orden del plan y saltando los casos que ya no están en el catálogo.
    QStringList caseIds;
    for (const auto& id : report.toContinue(planCases))
        if (m_store.find(id)) caseIds << id;
    QHash<QString, RunRecord> resume;
    for (const auto& row : report.rows)
        if (row.executed && row.run.isBroken() && caseIds.contains(row.caseId)) resume.insert(row.caseId, row.run);
    if (caseIds.isEmpty()) return false;
    // Se copia antes de cerrar lo que estuviera en curso: cerrar escribe en el historial.
    const QString planId = cycle->planId;
    QStringList composition = cycle->caseIds;
    for (const auto& id : caseIds)
        if (!composition.contains(id)) composition << id;

    commitRun(false);
    closePlan();
    // Continuar es seguir con el mismo ciclo: se reabre y sólo se ponen en cola sus casos rotos. El aviso
    // de arranque lo lleva a la ronda abierta del issue (o abre la siguiente si la suya ya se cerró).
    if (!m_history.reopenPlan(planRunId, environment)) return false;
    m_history.setPlanCases(planRunId, composition);   // con lo que el plan ganó después de arrancarlo
    m_resume = resume;
    m_review.clear();
    m_planRunId = planRunId;
    m_queue = caseIds.mid(1);
    begin(caseIds.first());
    changed();
    emit planStarted(m_planRunId, planId);
    return true;
}

bool RunController::runInCycle(const QString& planRunId, const QStringList& caseIds, const QString& environment) {
    const PlanRun* cycle = m_history.findPlan(planRunId);
    if (!cycle || !cycle->isFinished()) return false;
    QStringList ids;
    for (const auto& id : caseIds)
        if (m_store.find(id) && !ids.contains(id)) ids << id;
    if (ids.isEmpty()) return false;
    // Cómo arranca cada uno depende de lo que el ciclo tiene de él: lo roto se retoma, lo superado se
    // revisa y lo que no tiene ejecución (o es nuevo en el ciclo) va entero.
    QHash<QString, RunRecord> resume, review;
    for (const auto& row : m_history.report(planRunId).rows) {
        if (!row.executed || !ids.contains(row.caseId)) continue;
        (row.run.isBroken() ? resume : review).insert(row.caseId, row.run);
    }
    // Se copia antes de cerrar lo que estuviera en curso: cerrar escribe en el historial.
    const QString planId = cycle->planId;
    QStringList composition = cycle->caseIds;
    for (const auto& id : ids)
        if (!composition.contains(id)) composition << id;

    commitRun(false);
    closePlan();
    if (!m_history.reopenPlan(planRunId, environment)) return false;
    m_history.setPlanCases(planRunId, composition);
    m_resume = resume;
    m_review = review;
    m_planRunId = planRunId;
    m_queue = ids.mid(1);
    begin(ids.first());
    changed();
    emit planStarted(m_planRunId, planId);
    return true;
}

QList<RunRecord> RunController::importIntoCycle(const QString& planRunId, const QStringList& runIds) {
    QList<RunRecord> out;
    const PlanRun* cycle = m_history.findPlan(planRunId);
    if (!cycle || !cycle->isFinished()) return out;
    // Las que siguen vivas (la de pantalla y las aparcadas) ya tienen su id reservado.
    QStringList held;
    if (!m_run.runId.isEmpty()) held << m_run.runId;
    for (const auto& p : m_parked) held << p.run.runId;
    for (const auto& id : runIds)
        if (const auto rec = m_history.importRun(planRunId, id, held)) out << *rec;
    return out;
}

RunController::PlanSync RunController::syncPlanCases(const QStringList& planCases) {
    PlanSync sync;
    const PlanRun* cycle = m_planRunId.isEmpty() ? nullptr : m_history.findPlan(m_planRunId);
    if (!cycle) return sync;
    const QStringList before = cycle->caseIds;
    // Lo que ya se tocó en el ciclo no sale: lo ejecutado, el caso en pantalla y los aparcados. Ni lo
    // que se añadió al arrancarlo sólo para este ciclo: nunca fue del plan.
    QSet<QString> touched(cycle->addedCaseIds.cbegin(), cycle->addedCaseIds.cend());
    for (const auto& run : m_history.runsForPlan(m_planRunId)) touched.insert(run.caseId);
    touched.insert(m_run.caseId);
    for (auto it = m_parked.cbegin(); it != m_parked.cend(); ++it) touched.insert(it.key());

    // Lo que se dejó fuera al arrancar no entra aunque el plan lo tenga (o lo vuelva a tener).
    for (const auto& id : planCases)
        if (!before.contains(id) && !cycle->excludedCaseIds.contains(id) && m_store.find(id)) sync.added << id;
    for (const auto& id : before)
        if (!planCases.contains(id) && !touched.contains(id)) sync.removed << id;
    if (sync.isEmpty()) return sync;

    // El orden es el del plan; lo que ya no está en él pero se queda (porque se tocó) va detrás.
    QStringList composition;
    for (const auto& id : planCases)
        if ((before.contains(id) || sync.added.contains(id)) && !sync.removed.contains(id)) composition << id;
    for (const auto& id : before)
        if (!composition.contains(id) && !sync.removed.contains(id)) composition << id;
    const QString planRunId = m_planRunId;
    m_history.setPlanCases(planRunId, composition);
    for (const auto& id : sync.removed) m_queue.removeAll(id);
    m_queue << sync.added;
    std::stable_sort(m_queue.begin(), m_queue.end(), [&composition](const QString& a, const QString& b) {
        return composition.indexOf(a) < composition.indexOf(b);
    });
    changed();
    emit planCasesChanged(planRunId, sync.added, sync.removed);
    return sync;
}

void RunController::restart() {
    if (m_run.paused) return;
    commitRun(false);
    begin(QString(m_run.caseId));   // la cola del plan no se toca: se repite este caso, no el plan
    changed();
}

void RunController::pause() {
    if (m_run.caseId.isEmpty() || m_run.paused) return;
    // Un ciclo se pausa entero y se aparca, dejando la ejecución libre para otro.
    if (!m_planRunId.isEmpty()) {
        suspendCycle();
        return;
    }
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

void RunController::setAside() {
    holdStep();
    if (reviewUntouched()) return;   // nada que guardar: el caso sigue como se archivó
    m_parked.insert(m_run.caseId, ParkedRun{m_run, m_continuesRunId});
    // El que se deja vuelve a la cola en su sitio del plan, para que «Siguiente caso» siga el orden.
    const QStringList order = planCases();
    m_queue << m_run.caseId;
    std::stable_sort(m_queue.begin(), m_queue.end(), [&order](const QString& a, const QString& b) {
        return order.indexOf(a) < order.indexOf(b);
    });
}

bool RunController::goToCase(const QString& caseId) {
    if (m_planRunId.isEmpty() || m_run.caseId.isEmpty() || m_run.paused || !m_queue.contains(caseId)) return false;
    m_queue.removeAll(caseId);
    setAside();
    enterCase(caseId);
    changed();
    return true;
}

std::optional<RunRecord> RunController::archivedRun(const QString& caseId) const {
    if (m_planRunId.isEmpty()) return std::nullopt;
    // La que cuenta en el informe: la última del caso en el ciclo.
    for (const auto& row : m_history.report(m_planRunId).rows)
        if (row.caseId == caseId && row.executed) return row.run;
    return std::nullopt;
}

bool RunController::reviewUntouched() const {
    if (m_run.reviewOf.isEmpty()) return false;
    const RunRecord* archived = m_history.findRun(m_run.reviewOf);
    if (!archived) return false;
    for (int i = 0; i < m_run.results.size(); ++i) {
        const StepRecord& r = m_run.results[i];
        if (r.marked && !r.inherited) return false;   // se le dio veredicto de nuevo
        const QString before = i < archived->steps.size() ? archived->steps[i].note : QString();
        const QString note = i == m_run.idx ? m_run.note : r.note;
        if (note != before) return false;
    }
    // Lo que se captura revisándolo es evidencia nueva, todavía sin sellar en ninguna ejecución.
    const TestCase* c = m_store.find(m_run.caseId);
    return !c || c->shotsOfRun(QString()).isEmpty();
}

bool RunController::reviewCase(const QString& caseId) {
    if (m_planRunId.isEmpty() || m_run.caseId.isEmpty() || m_run.paused || caseId == m_run.caseId || m_queue.contains(caseId)) return false;
    const TestCase* c = m_store.find(caseId);
    const std::optional<RunRecord> archived = archivedRun(caseId);
    if (!c || !archived) return false;
    setAside();

    m_run = RunState{};
    m_run.caseId = caseId;
    m_run.runId = reserveRunId();   // por si se reporta un bug mientras se revisa
    m_run.startedAt = QDateTime::currentDateTime();
    m_run.results = QList<StepRecord>(c->steps.size());
    reviewFrom(*archived, *c);
    m_store.select(caseId);
    changed();
    return true;
}

void RunController::reviewFrom(const RunRecord& archived, const TestCase& c) {
    m_run.reviewOf = archived.id;
    // Se continúa la archivada: sus pasos llegan heredados, con su nota y su evidencia, y lo que se
    // vuelva a marcar pasa a ser de esta ejecución. Como al continuar un ciclo, sólo mientras el paso
    // siga diciendo lo mismo: uno que hoy es otro queda pendiente.
    m_continuesRunId = archived.id;
    for (int i = 0; i < m_run.results.size() && i < archived.steps.size() && i < c.steps.size(); ++i) {
        const RunRecordStep& step = archived.steps[i];
        if (c.steps[i].action != step.action || c.steps[i].data != step.data || c.steps[i].expected != step.expected) break;
        m_run.results[i].result = step.result;
        m_run.results[i].note = step.note;
        m_run.results[i].marked = true;
        m_run.results[i].inherited = true;
    }
    // Se abre en el primer paso para recorrerlo, aunque esté todo marcado: es lo que se viene a mirar.
    m_run.idx = 0;
    m_run.note = m_run.results.isEmpty() ? QString() : m_run.results.first().note;
    m_run.finished = m_run.results.isEmpty();
    m_run.stepElapsedSecs = 0;
    startStepClock();
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
    // Revisar sin cambiar nada no es otra ejecución: lo archivado se queda como estaba.
    if (reviewUntouched()) return;
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
    // Al retomar otra, la evidencia que ésta tenía se queda también en los pasos que se volvieron a
    // marcar (los heredados ya la traen): volver a probar un paso suma capturas, no las reemplaza.
    if (!m_continuesRunId.isEmpty()) {
        QList<int> retested;
        for (int i = 0; i < c->steps.size(); ++i)
            if (i >= rec.steps.size() || !rec.steps[i].inherited) retested << i + 1;
        for (const auto& shot : m_history.evidenceOfSteps(m_continuesRunId, retested))
            if (!rec.pinnedShots.contains(shot.id)) rec.pinnedShots << shot.id;
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
    m_review.clear();   // las revisiones que no se llegaron a abrir no pasan a otro ciclo
    m_run = current;
    m_continuesRunId = continues;
    const QString id = m_planRunId;
    m_planRunId.clear();
    m_queue.clear();
    m_history.finishPlan(id);
    emit planCompleted(id);
}

QString RunController::nextCase() const {
    if (m_queue.isEmpty()) return QString();
    // La cola va en el orden del ciclo; lo que queda detrás del caso de pantalla (porque se saltó a éste
    // desde la lista) no es «el siguiente».
    const QStringList order = planCases();
    const int current = order.indexOf(m_run.caseId);
    if (current < 0) return m_queue.first();
    for (const auto& id : m_queue)
        if (order.indexOf(id) > current || order.indexOf(id) < 0) return id;
    return QString();
}

bool RunController::finish() {
    if (m_run.caseId.isEmpty() || m_run.paused) return false;
    const QString next = nextCase();
    commitRun(true);
    if (!next.isEmpty()) {
        m_queue.removeAll(next);
        enterCase(next);
        changed();
        return true;
    }
    // Sin siguiente se termina el ciclo: lo que se dejó a medias atrás se archiva con lo marcado.
    for (const auto& p : std::as_const(m_parked)) {
        m_run = p.run;
        m_continuesRunId = p.continuesRunId;
        commitRun(true);
    }
    m_parked.clear();
    m_run = RunState{};
    closePlan();
    changed();
    return false;
}

void RunController::abandon(bool keepProgress) {
    if (keepProgress) {
        // Guardar lo hecho: el caso de pantalla y los aparcados se archivan con lo que tengan marcado
        // (lo pendiente, N/A); una revisión sin cambios no archiva nada. Los que no se empezaron, no.
        commitRun(true);
        const RunState current = m_run;
        const QString continues = m_continuesRunId;
        for (const auto& p : std::as_const(m_parked)) {
            m_run = p.run;
            m_continuesRunId = p.continuesRunId;
            commitRun(true);
        }
        m_parked.clear();
        m_run = current;
        m_continuesRunId = continues;
    } else {
        commitRun(false);
    }
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
    if (m_session->saveSession(snapshot())) return true;
    emit saveFailed(tr("la ejecución en curso"));
    return false;
}

} // namespace qaflow
