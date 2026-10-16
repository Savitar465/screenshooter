#include "RunHistoryStore.h"

#include "application/BugStore.h"
#include "application/TestCaseStore.h"

#include <algorithm>

namespace qaflow {

namespace {
/// Siguiente id "PREFIX-0001" a partir del mayor número ya usado.
template <typename T>
QString nextId(const QList<T>& items, const QString& prefix) {
    int maxNum = 0;
    for (const auto& it : items) {
        bool ok = false;
        const int n = it.id.mid(prefix.size()).toInt(&ok);
        if (ok) maxNum = std::max(maxNum, n);
    }
    return prefix + QStringLiteral("%1").arg(maxNum + 1, 4, 10, QLatin1Char('0'));
}

RunOutcome outcomeOf(Verdict v) {
    switch (v) {
        case Verdict::Superado: return RunOutcome::Passed;
        case Verdict::Fallido: return RunOutcome::Failed;
        case Verdict::Bloqueado: return RunOutcome::Blocked;
    }
    return RunOutcome::None;
}
} // namespace

RunHistoryStore::RunHistoryStore(std::shared_ptr<IRunHistoryRepository> repo, TestCaseStore& cases, QObject* parent)
    : QObject(parent), m_repo(std::move(repo)), m_cases(cases) {}

void RunHistoryStore::load() {
    auto loaded = m_repo ? m_repo->loadHistory() : std::nullopt;
    m_history = loaded ? *loaded : RunHistory{};
    emit historyChanged();
}

const RunRecord* RunHistoryStore::findRun(const QString& id) const {
    auto it = std::find_if(m_history.runs.cbegin(), m_history.runs.cend(), [&](const RunRecord& r) { return r.id == id; });
    return it == m_history.runs.cend() ? nullptr : &*it;
}

QList<Screenshot> RunHistoryStore::evidenceOf(const RunRecord& run) const {
    const TestCase* c = m_cases.find(run.caseId);
    QList<Screenshot> out;
    if (!run.continuesRunId.isEmpty()) {
        // Lo que no se asignó a ningún paso (0) no se vuelve a probar al retomar: sigue siendo del caso.
        QList<int> inherited{0};
        for (int i = 0; i < run.steps.size(); ++i) if (run.steps[i].inherited) inherited << i + 1;
        // Primero lo heredado, que es de los pasos anteriores al que se retomó.
        out = evidenceOfSteps(run.continuesRunId, inherited);
    }
    if (c) {
        // Las que conserva aunque la original las quitara, en el orden del caso, y luego las suyas.
        for (const auto& s : c->shots)
            if (run.pinnedShots.contains(s.id) && std::none_of(out.cbegin(), out.cend(), [&s](const Screenshot& o) { return o.id == s.id; })) out << s;
        out += c->shotsOfRun(run.id);
    }
    if (!run.unlinkedShots.isEmpty())
        out.erase(std::remove_if(out.begin(), out.end(), [&run](const Screenshot& s) { return run.unlinkedShots.contains(s.id); }), out.end());
    return out;
}

bool RunHistoryStore::removeEvidence(const QString& runId, int shotId) {
    auto has = [shotId](const QList<Screenshot>& shots) {
        return std::any_of(shots.cbegin(), shots.cend(), [shotId](const Screenshot& s) { return s.id == shotId; });
    };
    const RunRecord* run = findRun(runId);
    if (!run || !has(evidenceOf(*run))) return false;
    const QString caseId = run->caseId;
    // Quién más la tiene ahora: ésas no la pierden.
    QStringList others;
    for (const auto& r : m_history.runs)
        if (r.id != runId && r.caseId == caseId && has(evidenceOf(r))) others << r.id;
    if (others.isEmpty()) {
        // Ya no la tiene nadie más: se borra del caso, con «Deshacer» como cualquier evidencia.
        m_cases.removeShot(caseId, shotId);
        return true;
    }
    for (auto& r : m_history.runs)
        if (r.id == runId) {
            r.pinnedShots.removeAll(shotId);
            if (!r.unlinkedShots.contains(shotId)) r.unlinkedShots << shotId;
        }
    // Las que la tenían a través de ésta (la heredan o la trajeron) la conservan por su cuenta.
    for (auto& r : m_history.runs)
        if (others.contains(r.id) && !has(evidenceOf(r))) r.pinnedShots << shotId;
    persist();
    return true;
}

QSet<int> RunHistoryStore::pinnedOutside(const QStringList& runIds) const {
    QSet<int> out;
    for (const auto& r : m_history.runs)
        if (!runIds.contains(r.id)) for (int id : r.pinnedShots) out.insert(id);
    return out;
}

QList<Screenshot> RunHistoryStore::evidenceOfSteps(const QString& runId, const QList<int>& steps) const {
    const RunRecord* run = steps.isEmpty() ? nullptr : findRun(runId);
    if (!run) return {};
    QList<Screenshot> out;
    for (const auto& shot : evidenceOf(*run)) if (steps.contains(shot.step)) out << shot;
    return out;
}

const PlanRun* RunHistoryStore::findPlan(const QString& id) const {
    auto it = std::find_if(m_history.plans.cbegin(), m_history.plans.cend(), [&](const PlanRun& p) { return p.id == id; });
    return it == m_history.plans.cend() ? nullptr : &*it;
}

QList<RunRecord> RunHistoryStore::runsForCase(const QString& caseId) const {
    QList<RunRecord> out;
    for (const auto& r : m_history.runs) if (r.caseId == caseId) out.prepend(r);
    return out;
}

QList<RunRecord> RunHistoryStore::runsForPlan(const QString& planRunId) const {
    QList<RunRecord> out;
    for (const auto& r : m_history.runs) if (r.planRunId == planRunId) out.append(r);
    return out;
}

PlanReport RunHistoryStore::report(const QString& planRunId) const {
    const PlanRun* plan = findPlan(planRunId);
    if (!plan) return PlanReport{};
    PlanReport report = PlanReport::build(*plan, runsForPlan(planRunId), [this](const QString& caseId) {
        const TestCase* c = m_cases.find(caseId);
        return c ? PlanReport::CaseInfo{c->title, c->jiraKey} : PlanReport::CaseInfo{};
    }, m_bugs ? m_bugs->issues() : QList<IssueLink>{});
    // Una ejecución traída de fuera trae sus bugs: salieron de otro ciclo (o de una ejecución suelta),
    // así que el informe no los atribuye a éste por su `planRunId`, pero son los de su veredicto.
    for (auto& row : report.rows) {
        if (!row.executed || !reachesImport(row.run)) continue;
        bool added = false;
        for (const auto& bug : bugsOfRun(row.run)) {
            const bool known = std::any_of(row.bugs.cbegin(), row.bugs.cend(), [&bug](const IssueLink& b) {
                return b.key == bug.key && b.runId == bug.runId && b.createdAt == bug.createdAt;
            });
            if (!known) { row.bugs.append(bug); added = true; }
        }
        if (added)
            std::sort(row.bugs.begin(), row.bugs.end(), [](const IssueLink& a, const IssueLink& b) { return a.createdAt > b.createdAt; });
    }
    return report;
}

bool RunHistoryStore::reachesImport(const RunRecord& run) const {
    // Acotado como en `bugsOfRun`, por si un history.json editado a mano cerrara la cadena en círculo.
    int guard = 0;
    for (const RunRecord* r = &run; r && guard < 100; ++guard) {
        if (r->imported) return true;
        r = r->continuesRunId.isEmpty() ? nullptr : findRun(r->continuesRunId);
    }
    return false;
}

QString RunHistoryStore::startPlan(const QString& name, const QStringList& caseIds, const QString& planId,
                                  const QString& environment, const QStringList& excluded, const QStringList& added) {
    if (caseIds.isEmpty()) return {};
    PlanRun p;
    p.id = nextId(m_history.plans, QStringLiteral("PR-"));
    p.planId = planId;
    p.name = name;
    p.caseIds = caseIds;
    for (const auto& id : excluded)
        if (!caseIds.contains(id) && !p.excludedCaseIds.contains(id)) p.excludedCaseIds << id;
    for (const auto& id : added)
        if (caseIds.contains(id) && !p.addedCaseIds.contains(id)) p.addedCaseIds << id;
    p.environment = environment.trimmed();
    p.startedAt = QDateTime::currentDateTime();
    m_history.plans.append(p);
    persist();
    return p.id;
}

void RunHistoryStore::noteCycleRevision(const QString& planRunId, const QString& issueId, int revision) {
    if (issueId.trimmed().isEmpty()) return;
    for (auto& p : m_history.plans) {
        if (p.id != planRunId) continue;
        if (p.issueId == issueId && p.revision == revision) return;
        p.issueId = issueId;
        p.revision = revision;
        persist();
        return;
    }
}

QString RunHistoryStore::lastEnvironment() const {
    // De atrás hacia delante: los ciclos se añaden en orden, así que el último que lo indique es el
    // más reciente.
    for (auto it = m_history.plans.crbegin(); it != m_history.plans.crend(); ++it)
        if (!it->environment.trimmed().isEmpty()) return it->environment.trimmed();
    return {};
}

void RunHistoryStore::finishPlan(const QString& planRunId) {
    for (auto& p : m_history.plans) {
        if (p.id != planRunId || p.isFinished()) continue;
        p.finishedAt = QDateTime::currentDateTime();
        p.pausedAt = QDateTime();   // terminar es salir de la pausa
        persist();
        return;
    }
}

void RunHistoryStore::setPlanPaused(const QString& planRunId, bool paused) {
    for (auto& p : m_history.plans) {
        if (p.id != planRunId) continue;
        if (p.isFinished() || paused == p.pausedAt.isValid()) return;
        p.pausedAt = paused ? QDateTime::currentDateTime() : QDateTime();
        persist();
        return;
    }
}

void RunHistoryStore::setPlanCases(const QString& planRunId, const QStringList& caseIds) {
    for (auto& p : m_history.plans) {
        if (p.id != planRunId) continue;
        const int excluded = int(p.excludedCaseIds.size());
        p.excludedCaseIds.erase(std::remove_if(p.excludedCaseIds.begin(), p.excludedCaseIds.end(),
                                               [&caseIds](const QString& id) { return caseIds.contains(id); }),
                                p.excludedCaseIds.end());
        if (p.caseIds == caseIds && p.excludedCaseIds.size() == excluded) return;
        p.caseIds = caseIds;
        persist();
        return;
    }
}

bool RunHistoryStore::reopenPlan(const QString& planRunId, const QString& environment) {
    for (auto& p : m_history.plans) {
        if (p.id != planRunId) continue;
        if (!p.isFinished()) return false;
        p.finishedAt = QDateTime();
        p.pausedAt = QDateTime();
        if (p.environment.trimmed().isEmpty()) p.environment = environment.trimmed();
        ++p.continuations;
        persist();
        return true;
    }
    return false;
}

int RunHistoryStore::adoptLooseEvidence(const QString& runningCaseId) {
    // Los ids primero: adoptar reescribe la lista de casos y dejaría la iteración colgando.
    QStringList pending;
    for (const auto& c : m_cases.cases()) {
        if (c.id == runningCaseId) continue;
        for (const auto& s : c.shots)
            if (s.runId.isEmpty()) { pending << c.id; break; }
    }
    int moved = 0;
    for (const auto& caseId : pending) {
        const QList<RunRecord> runs = runsForCase(caseId);   // la más reciente primero
        moved += m_cases.adoptLooseShots(caseId, runs.isEmpty() ? QString() : runs.first().id);
    }
    return moved;
}

void RunHistoryStore::markPublished(const QString& planRunId, const QString& zephyrCycleId) {
    if (zephyrCycleId.trimmed().isEmpty()) return;
    for (auto& p : m_history.plans) {
        if (p.id != planRunId) continue;
        // Republicar crea otro ciclo en Zephyr: el que vale es el último, que es donde están estos
        // resultados ahora.
        p.zephyrCycleId = zephyrCycleId.trimmed();
        p.publishedAt = QDateTime::currentDateTime();
        persist();
        return;
    }
}

void RunHistoryStore::assignTestKeys(const QHash<QString, QString>& testKeyByRunId) {
    bool changed = false;
    for (auto& r : m_history.runs) {
        const QString key = testKeyByRunId.value(r.id).trimmed();
        if (key.isEmpty() || r.testKey == key) continue;
        r.testKey = key;
        changed = true;
    }
    if (changed) persist();
}

QString RunHistoryStore::reserveRunId(const QStringList& held) const {
    const QString prefix = QStringLiteral("R-");
    QString id = nextId(m_history.runs, prefix);
    int number = id.mid(prefix.size()).toInt();
    for (const QString& other : held) {
        bool ok = false;
        const int n = other.mid(prefix.size()).toInt(&ok);
        if (ok && other.startsWith(prefix) && n >= number) number = n + 1;
    }
    return prefix + QStringLiteral("%1").arg(number, 4, 10, QLatin1Char('0'));
}

QList<IssueLink> RunHistoryStore::bugsOfRun(const RunRecord& run) const {
    if (!m_bugs) return {};
    // La cadena de ejecuciones que se retoman es finita, pero se acota por si un history.json editado a
    // mano la cerrara en círculo.
    QList<const RunRecord*> chain{&run};
    for (const RunRecord* r = &run; !r->continuesRunId.isEmpty() && chain.size() < 100;) {
        r = findRun(r->continuesRunId);
        if (!r || chain.contains(r)) break;
        chain << r;
    }
    QList<IssueLink> out;
    for (const auto& bug : m_bugs->issues())
        if (std::any_of(chain.cbegin(), chain.cend(), [&bug](const RunRecord* r) { return PlanReport::foundIn(*r, bug); })) out << bug;
    std::sort(out.begin(), out.end(), [](const IssueLink& a, const IssueLink& b) { return a.createdAt > b.createdAt; });
    return out;
}

QList<IssueLink> RunHistoryStore::bugsOfRun(const QString& runId) const {
    const RunRecord* run = runId.isEmpty() ? nullptr : findRun(runId);
    return run ? bugsOfRun(*run) : QList<IssueLink>{};
}

RunRecord RunHistoryStore::addRun(RunRecord record) {
    // El id reservado al arrancar la ejecución es con el que la conocen los bugs que se reportaron
    // mientras corría; sólo se descarta si entretanto lo cogió otra.
    const bool taken = record.id.trimmed().isEmpty() ||
                       std::any_of(m_history.runs.cbegin(), m_history.runs.cend(),
                                   [&record](const RunRecord& r) { return r.id == record.id; });
    if (taken) record.id = nextId(m_history.runs, QStringLiteral("R-"));
    m_history.runs.append(record);
    persist();
    return record;
}

bool RunHistoryStore::removePlanRun(const QString& planRunId) {
    auto plan = std::find_if(m_history.plans.begin(), m_history.plans.end(), [&](const PlanRun& p) { return p.id == planRunId; });
    if (plan == m_history.plans.end()) return false;

    // Los casos cuya «última ejecución» es una de las que se van: sólo a ésos hay que recalcularla.
    QStringList runIds, staleCases;
    for (const auto& r : m_history.runs) {
        if (r.planRunId != planRunId) continue;
        runIds << r.id;
        const QList<RunRecord> ofCase = runsForCase(r.caseId);   // la más reciente primero
        if (!ofCase.isEmpty() && ofCase.first().planRunId == planRunId && !staleCases.contains(r.caseId)) staleCases << r.caseId;
    }
    m_history.plans.erase(plan);
    m_history.runs.erase(std::remove_if(m_history.runs.begin(), m_history.runs.end(), [&](const RunRecord& r) { return r.planRunId == planRunId; }),
                         m_history.runs.end());

    m_cases.releaseShotsOfRuns(runIds, pinnedOutside(runIds));
    refreshLastRun(staleCases);
    persist();
    return true;
}

void RunHistoryStore::refreshLastRun(const QStringList& caseIds) {
    for (const auto& caseId : caseIds) {
        // La más reciente que quede es la última que se archivó (se archivan en orden), salvo las traídas
        // a un ciclo, que no son una ejecución nueva del caso.
        LastRun last;
        for (auto it = m_history.runs.crbegin(); it != m_history.runs.crend(); ++it) {
            if (it->caseId != caseId || it->imported) continue;
            last = LastRun{outcomeOf(it->verdict), it->finishedAt};
            break;
        }
        m_cases.updateCase(caseId, [last](TestCase& c) { c.lastRun = last; });
    }
}

std::optional<RunRecord> RunHistoryStore::importRun(const QString& planRunId, const QString& sourceRunId, const QStringList& held) {
    const PlanRun* plan = findPlan(planRunId);
    const RunRecord* source = findRun(sourceRunId);
    if (!plan || !source || source->planRunId == planRunId) return std::nullopt;
    const bool already = std::any_of(m_history.runs.cbegin(), m_history.runs.cend(), [&](const RunRecord& r) {
        return r.planRunId == planRunId && r.imported && r.continuesRunId == sourceRunId;
    });
    if (already) return std::nullopt;

    // La traída es la original tal cual, en este ciclo: sus pasos, su veredicto, su tiempo y sus horas
    // (se probó entonces). Todos sus pasos son heredados: no se volvió a probar nada.
    RunRecord rec = *source;
    rec.id = reserveRunId(held);
    rec.planRunId = planRunId;
    rec.testKey.clear();   // al publicar este ciclo tendrá su propio Test
    rec.continuesRunId = source->id;
    rec.imported = true;
    for (auto& step : rec.steps) step.inherited = true;
    for (auto& p : m_history.plans)
        if (p.id == planRunId) {
            if (!p.caseIds.contains(rec.caseId)) p.caseIds << rec.caseId;
            p.excludedCaseIds.removeAll(rec.caseId);
        }
    // No cambia la «última ejecución» del caso: no se ha vuelto a ejecutar.
    m_history.runs.append(rec);
    persist();
    return rec;
}

QList<RunRecord> RunHistoryStore::dependentsOf(const QStringList& runIds) const {
    QList<RunRecord> out;
    for (const auto& r : m_history.runs)
        if (!runIds.contains(r.id) && runIds.contains(r.continuesRunId)) out << r;
    return out;
}

QString RunHistoryStore::cannotRemoveCase(const QString& planRunId, const QString& caseId) const {
    const PlanRun* plan = findPlan(planRunId);
    if (!plan) return tr("El ciclo %1 no existe").arg(planRunId);
    if (!plan->isFinished()) return tr("El ciclo %1 sigue en curso").arg(planRunId);
    if (!plan->caseIds.contains(caseId)) return tr("%1 no es del ciclo %2").arg(caseId, planRunId);
    if (plan->caseIds.size() == 1) return tr("%1 es el único caso del ciclo: elimina el informe entero").arg(caseId);
    QStringList ids;
    for (const auto& r : m_history.runs)
        if (r.planRunId == planRunId && r.caseId == caseId) ids << r.id;
    const QList<RunRecord> dependents = dependentsOf(ids);
    if (!dependents.isEmpty()) {
        const RunRecord& d = dependents.first();
        const QString where = d.planRunId.isEmpty() ? tr("una ejecución suelta") : tr("el ciclo %1").arg(d.planRunId);
        return d.imported ? tr("Su ejecución %1 se trajo a %2 (como %3): quítala de allí antes").arg(d.continuesRunId, where, d.id)
                          : tr("Su ejecución %1 la retoma %2 en %3: quítala de allí antes").arg(d.continuesRunId, d.id, where);
    }
    return {};
}

bool RunHistoryStore::removeCaseFromCycle(const QString& planRunId, const QString& caseId) {
    if (!cannotRemoveCase(planRunId, caseId).isEmpty()) return false;
    QStringList runIds;
    for (const auto& r : m_history.runs)
        if (r.planRunId == planRunId && r.caseId == caseId) runIds << r.id;
    // Si su última ejecución es de las que se van, hay que recalcularla.
    const QList<RunRecord> ofCase = runsForCase(caseId);   // la más reciente primero
    const bool stale = !ofCase.isEmpty() && runIds.contains(ofCase.first().id);
    for (auto& p : m_history.plans)
        if (p.id == planRunId) p.caseIds.removeAll(caseId);
    m_history.runs.erase(std::remove_if(m_history.runs.begin(), m_history.runs.end(), [&](const RunRecord& r) { return runIds.contains(r.id); }),
                         m_history.runs.end());
    // Las evidencias de una traída son de la original: sólo se liberan las tomadas en estas ejecuciones.
    m_cases.releaseShotsOfRuns(runIds, pinnedOutside(runIds));
    if (stale) refreshLastRun({caseId});
    persist();
    return true;
}

bool RunHistoryStore::save() {
    if (!m_repo) return false;
    if (m_repo->saveHistory(m_history)) return true;
    emit saveFailed(tr("el historial de ejecuciones"));
    return false;
}

void RunHistoryStore::persist() {
    save();
    emit historyChanged();
}

} // namespace qaflow
