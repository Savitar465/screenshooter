#include "IssueStore.h"

#include "application/PlanStore.h"
#include "application/RunHistoryStore.h"

#include <QSet>

#include <algorithm>

namespace qaflow {

namespace {
/// La conexión tal y como se guarda: sin espacios ni barra final.
QString cleanConnection(const QString& connection) {
    QString c = connection.trimmed();
    while (c.endsWith(QLatin1Char('/'))) c.chop(1);
    return c;
}
} // namespace

IssueStore::IssueStore(std::shared_ptr<IIssueRepository> repo, QObject* parent) : QObject(parent), m_repo(std::move(repo)) {}

void IssueStore::load() {
    const std::optional<QList<Issue>> loaded = m_repo ? m_repo->loadIssues() : std::optional<QList<Issue>>(QList<Issue>{});
    m_readOnly = !loaded.has_value();
    m_issues = loaded.value_or(QList<Issue>{});
    // Antes, cerrar una revisión como Observado daba el issue por finalizado: un requerimiento
    // observado no ha terminado, vuelve a pruebas cuando se corrija.
    for (Issue& i : m_issues)
        if (i.state == IssueState::Done && !i.currentRevision() && i.lastOutcome() == QaOutcome::Observado) i.state = IssueState::Testing;
    if (!find(m_selectedId)) m_selectedId = m_issues.isEmpty() ? QString() : m_issues.first().id;
    emit issuesChanged();
    emit selectionChanged(m_selectedId);
    if (m_readOnly) emit loadFailed(tr("No se pudieron leer los issues del proyecto: no se guardarán cambios en ellos para no perderlos"));
}

bool IssueStore::save() {
    if (!m_readOnly && (!m_repo || m_repo->saveIssues(m_issues))) return true;
    emit saveFailed(tr("los issues"));
    return false;
}

void IssueStore::persist(const QString& changedId) {
    save();
    emit issuesChanged();
    if (!changedId.isEmpty()) emit issueChanged(changedId);
}

const Issue* IssueStore::find(const QString& id) const {
    const auto it = std::find_if(m_issues.cbegin(), m_issues.cend(), [&id](const Issue& i) { return i.id == id; });
    return it == m_issues.cend() ? nullptr : &*it;
}

Issue* IssueStore::findMutable(const QString& id) {
    const auto it = std::find_if(m_issues.begin(), m_issues.end(), [&id](const Issue& i) { return i.id == id; });
    return it == m_issues.end() ? nullptr : &*it;
}

QList<Issue> IssueStore::issuesForPlan(const QString& planId) const {
    QList<Issue> out;
    for (const auto& i : m_issues)
        if (i.planIds.contains(planId)) out << i;
    return out;
}

const Issue* IssueStore::findByRequirement(const QString& connection, const QString& requirementId) const {
    const auto it = std::find_if(m_issues.cbegin(), m_issues.cend(),
                                 [&](const Issue& i) { return i.testsRequirement(connection, requirementId); });
    return it == m_issues.cend() ? nullptr : &*it;
}

int IssueStore::changedCount() const {
    return int(std::count_if(m_issues.cbegin(), m_issues.cend(), [](const Issue& i) { return !i.requirement.changes.isEmpty(); }));
}

void IssueStore::select(const QString& id) {
    if (id == m_selectedId || (!id.isEmpty() && !find(id))) return;
    m_selectedId = id;
    emit selectionChanged(id);
}

QString IssueStore::nextId() const {
    int maxNum = 0;
    for (const auto& i : m_issues) {
        bool ok = false;
        const int n = i.id.mid(3).toInt(&ok);   // IS-0001
        if (ok) maxNum = std::max(maxNum, n);
    }
    return QStringLiteral("IS-%1").arg(maxNum + 1, 4, 10, QLatin1Char('0'));
}

QString IssueStore::createIssue(const QString& title) {
    Issue issue;
    issue.id = nextId();
    issue.title = title.trimmed().isEmpty() ? tr("Issue sin título") : title.trimmed();
    issue.createdAt = issue.updatedAt = QDateTime::currentDateTime();
    m_issues.append(issue);
    persist(issue.id);
    m_selectedId = issue.id;
    emit selectionChanged(issue.id);
    return issue.id;
}

void IssueStore::updateIssue(const QString& id, const std::function<void(Issue&)>& mutate) {
    Issue* issue = findMutable(id);
    if (!issue) return;
    mutate(*issue);
    issue->id = id;   // la identidad no cambia desde fuera
    issue->updatedAt = QDateTime::currentDateTime();
    persist(id);
}

void IssueStore::removeIssue(const QString& id) {
    const auto it = std::find_if(m_issues.begin(), m_issues.end(), [&id](const Issue& i) { return i.id == id; });
    if (it == m_issues.end()) return;
    const qsizetype index = it - m_issues.begin();
    m_issues.erase(it);
    persist();
    if (m_selectedId != id) return;
    m_selectedId = m_issues.isEmpty() ? QString() : m_issues[std::min(index, m_issues.size() - 1)].id;
    emit selectionChanged(m_selectedId);
}

void IssueStore::linkPlan(const QString& issueId, const QString& planId) {
    const Issue* issue = find(issueId);
    if (!issue || planId.isEmpty() || issue->planIds.contains(planId)) return;
    updateIssue(issueId, [&planId](Issue& i) {
        i.planIds << planId;
        // Ya hay con qué probar: el issue deja de estar pendiente. Un estado más avanzado no se toca.
        if (i.state == IssueState::Pending) i.state = IssueState::Preparing;
    });
}

void IssueStore::unlinkPlan(const QString& issueId, const QString& planId) {
    const Issue* issue = find(issueId);
    if (!issue || !issue->planIds.contains(planId)) return;
    updateIssue(issueId, [&planId](Issue& i) { i.planIds.removeAll(planId); });
}

// ---- Flujo de la revisión ----------------------------------------------------------------------

IssueRevision& IssueStore::revisionFor(Issue& issue, int number) {
    if (issue.revisions.isEmpty()) appendRevision(issue);
    if (number > 0)
        for (auto& round : issue.revisions)
            if (round.number == number) return round;
    return issue.revisions.last();
}

QString IssueStore::validPhase(const Issue& issue, const QString& phase) const {
    for (const QString& p : phasesOf(issue))
        if (p.compare(phase.trimmed(), Qt::CaseInsensitive) == 0) return p;
    return {};
}

const IssueRevision& IssueStore::appendRevision(Issue& issue, const QString& phase) const {
    IssueRevision next;
    next.number = issue.revisions.isEmpty() ? 1 : issue.revisions.last().number + 1;
    const QString chosen = validPhase(issue, phase);
    next.phase = chosen.isEmpty() ? nextPhase(issue, phasesOf(issue)) : chosen;
    next.startedAt = QDateTime::currentDateTime();
    issue.revisions << next;
    return issue.revisions.last();
}

void IssueStore::setPhases(const QStringList& phases) {
    const QStringList clean = normalizedQaPhases(phases);
    if (clean == m_phases) return;
    m_phases = clean;
    emit issuesChanged();   // lo que se enseña de cada ronda (su fase, qué la cierra) depende de ellas
}

QStringList IssueStore::phasesOf(const Issue& issue) const {
    if (issue.phases.isEmpty()) return m_phases;
    // En el orden del proyecto; una que ya no está en él (se quitó después) va al final.
    QStringList out;
    for (const QString& phase : m_phases)
        if (issue.phases.contains(phase, Qt::CaseInsensitive)) out << phase;
    for (const QString& phase : issue.phases)
        if (!out.contains(phase.trimmed(), Qt::CaseInsensitive)) out << phase.trimmed();
    return normalizedQaPhases(out);
}

QString IssueStore::setIssuePhases(const QString& issueId, const QStringList& phases) {
    const Issue* issue = find(issueId);
    if (!issue) return tr("El issue ya no existe");
    QStringList chosen;
    for (const QString& phase : m_phases)
        if (phases.contains(phase, Qt::CaseInsensitive)) chosen << phase;
    for (const QString& phase : phases)
        if (!phase.trimmed().isEmpty() && !chosen.contains(phase.trimmed(), Qt::CaseInsensitive)) chosen << phase.trimmed();
    const QStringList effective = chosen.isEmpty() ? m_phases : chosen;
    // Las fases con rondas cerradas se quedan: sus actas y sus resultados son de ellas.
    const QStringList current = phasesOf(*issue);
    for (const auto& round : issue->revisions) {
        const QString used = phaseOf(round, current);
        if (!round.isOpen() && !effective.contains(used, Qt::CaseInsensitive))
            return tr("%1 ya tiene revisiones cerradas en %2: esa fase no se puede quitar").arg(issue->id, used);
    }
    // La ronda abierta en una fase que se quita pasa a la siguiente que quede (o a la última).
    QString moveTo;
    if (const IssueRevision* open = issue->currentRevision(); open && !effective.contains(phaseOf(*open, current), Qt::CaseInsensitive)) {
        const int from = int(current.indexOf(phaseOf(*open, current)));
        for (int k = from + 1; k < current.size() && moveTo.isEmpty(); ++k)
            if (effective.contains(current.at(k), Qt::CaseInsensitive)) moveTo = current.at(k);
        if (moveTo.isEmpty()) moveTo = effective.last();
    }
    const QStringList stored = effective == m_phases ? QStringList() : effective;
    if (stored == issue->phases && moveTo.isEmpty()) return {};
    updateIssue(issueId, [&stored, &moveTo](Issue& i) {
        i.phases = stored;
        if (!moveTo.isEmpty() && !i.revisions.isEmpty()) i.revisions.last().phase = moveTo;
    });
    return {};
}

void IssueStore::noteZephyrTests(const QString& issueId, const QHash<QString, QString>& tests) {
    if (!find(issueId) || tests.isEmpty()) return;
    updateIssue(issueId, [&tests](Issue& i) {
        for (auto it = tests.cbegin(); it != tests.cend(); ++it)
            if (!it.key().isEmpty() && !it.value().trimmed().isEmpty()) i.zephyr.tests.insert(it.key(), it.value().trimmed());
    });
}

void IssueStore::noteZephyrCycle(const QString& issueId, const QString& phase, const QString& cycleId, const QString& name) {
    const Issue* issue = find(issueId);
    if (!issue || cycleId.trimmed().isEmpty()) return;
    const QString key = IssueZephyr::phaseKey(phase);
    if (issue->zephyr.cycles.value(key) == cycleId.trimmed() && issue->zephyr.cycleNames.value(key) == name) return;
    updateIssue(issueId, [&](Issue& i) {
        i.zephyr.cycles.insert(key, cycleId.trimmed());
        i.zephyr.cycleNames.insert(key, name);
    });
}

void IssueStore::forgetZephyrCycle(const QString& issueId, const QString& phase) {
    if (!find(issueId)) return;
    const QString key = IssueZephyr::phaseKey(phase);
    updateIssue(issueId, [&key](Issue& i) {
        i.zephyr.cycles.remove(key);
        i.zephyr.cycleNames.remove(key);
    });
}

IssueStore::RevisionRef IssueStore::nextCycleContext(const QString& planId) const {
    RevisionRef next;
    if (planId.isEmpty()) return next;
    for (const auto& issue : m_issues) {
        if (!issue.planIds.contains(planId)) continue;
        next.issueId = issue.id;
        if (const IssueRevision* open = issue.currentRevision()) {
            next.revision = open->number;
            next.phase = phaseOf(*open, phasesOf(issue));
        } else {
            next.revision = issue.revisions.isEmpty() ? 1 : issue.revisions.last().number + 1;
            next.phase = nextPhase(issue, phasesOf(issue));
        }
        break;
    }
    return next;
}

IssueStore::RevisionRef IssueStore::notePlanStarted(const QString& planId, const QString& phase) {
    RevisionRef started;
    if (planId.isEmpty()) return started;
    QStringList changed;
    for (auto& issue : m_issues) {
        if (!issue.planIds.contains(planId)) continue;
        const bool wasTesting = issue.state == IssueState::Testing;
        const bool hadOpenRevision = issue.currentRevision() != nullptr;
        const QString chosen = validPhase(issue, phase);
        bool rephased = false;
        if (!hadOpenRevision) {
            appendRevision(issue, chosen);
        } else if (!chosen.isEmpty() && phaseOf(issue.revisions.last(), phasesOf(issue)) != chosen) {
            // Se arranca en otra fase una ronda que todavía no tenía ciclos: pasa a ser de ella.
            issue.revisions.last().phase = chosen;
            rephased = true;
        }
        issue.state = IssueState::Testing;
        // El ciclo se anota en el primer issue que lo agrupa: es el requerimiento cuyo control de
        // calidad se está haciendo, y su ronda abierta es la revisión a la que pertenece el ciclo.
        if (started.issueId.isEmpty()) {
            started.issueId = issue.id;
            started.revision = issue.revisions.last().number;
            started.phase = phaseOf(issue.revisions.last(), phasesOf(issue));
        }
        if (wasTesting && hadOpenRevision && !rephased) continue;   // ya estaba probando esta misma ronda
        issue.updatedAt = QDateTime::currentDateTime();
        changed << issue.id;
    }
    if (changed.isEmpty()) return started;
    persist();
    for (const auto& id : changed) emit issueChanged(id);
    return started;
}

int IssueStore::openRevision(const QString& issueId) {
    const Issue* found = find(issueId);
    if (!found) return 0;
    int number = 0;
    updateIssue(issueId, [this, &number](Issue& i) {
        if (const IssueRevision* open = i.currentRevision()) number = open->number;
        else number = appendRevision(i).number;
        i.state = IssueState::Testing;
    });
    return number;
}

void IssueStore::setRevisionRecord(const QString& issueId, const QualityRecord& record, const QString& documentPath,
                                   const QString& planRunId, int number) {
    if (!find(issueId)) return;
    updateIssue(issueId, [&](Issue& i) {
        IssueRevision& revision = revisionFor(i, number);
        revision.record = record;
        if (!planRunId.trimmed().isEmpty()) revision.planRunId = planRunId;
        if (documentPath.trimmed().isEmpty()) return;
        revision.documentPath = documentPath;
        revision.documentAt = QDateTime::currentDateTime();
    });
}

void IssueStore::setRevisionPublication(const QString& issueId, const RevisionPublication& publication, int number) {
    if (!find(issueId)) return;
    updateIssue(issueId, [&](Issue& i) { revisionFor(i, number).jira = publication; });
}

void IssueStore::setRevisionRegistration(const QString& issueId, const RevisionRegistration& registration, int number) {
    if (!find(issueId)) return;
    updateIssue(issueId, [&](Issue& i) { revisionFor(i, number).gesreq = registration; });
}

void IssueStore::closeRevision(const QString& issueId, QaOutcome outcome) {
    const Issue* issue = find(issueId);
    if (!issue || !issue->currentRevision()) return;
    updateIssue(issueId, [this, outcome](Issue& i) {
        IssueRevision& revision = i.revisions.last();
        const QStringList phases = phasesOf(i);
        if (revision.phase.isEmpty()) revision.phase = phaseOf(revision, phases);
        revision.outcome = outcome;
        revision.closedAt = QDateTime::currentDateTime();
        // Sólo el Conforme de la última fase termina el trabajo. Conforme en otra aprueba ésa y queda
        // la siguiente; uno observado sigue pendiente de que lo corrijan y se vuelva a probar. En los
        // dos casos eso abre la revisión siguiente (notePlanStarted / openRevision), no reabre ésta.
        i.state = closesRequirement(revision, phases) ? IssueState::Done : IssueState::Testing;
    });
}

// ---- Importación -------------------------------------------------------------------------------

QList<IssueStore::ImportCandidate> IssueStore::previewImport(const QList<ExternalRequirement>& requirements, const QString& connection) const {
    QList<ImportCandidate> out;
    QSet<QString> seen;
    for (const auto& r : requirements) {
        if (r.id.isEmpty() || seen.contains(r.id)) continue;
        seen.insert(r.id);
        ImportCandidate candidate;
        candidate.requirement = r;
        if (const Issue* existing = findByRequirement(connection, r.id)) {
            candidate.issueId = existing->id;
            candidate.changes = diffRequirement(existing->requirement.data, r);
            candidate.kind = candidate.changes.isEmpty() ? ImportCandidate::Kind::Unchanged : ImportCandidate::Kind::Changed;
        }
        out << candidate;
    }
    return out;
}

IssueStore::ImportResult IssueStore::importRequirements(const QList<ExternalRequirement>& requirements, const QString& connection,
                                                        const QDateTime& fetchedAt) {
    ImportResult result;
    const QString conn = cleanConnection(connection);
    const QDateTime now = QDateTime::currentDateTime();
    QSet<QString> seen;
    bool touched = false;
    for (const auto& r : requirements) {
        if (r.id.isEmpty() || seen.contains(r.id)) continue;
        seen.insert(r.id);
        touched = true;
        if (const Issue* found = findByRequirement(conn, r.id)) {
            Issue* issue = findMutable(found->id);
            const QList<RequirementChange> changes = diffRequirement(issue->requirement.data, r);
            // Sólo lo extraído: título, notas, prioridad, estado y asociaciones son de QAflow.
            issue->requirement.data = r;
            issue->requirement.fetchedAt = fetchedAt;
            issue->requirement.missing = false;
            if (changes.isEmpty()) continue;
            issue->requirement.changes = mergeChanges(issue->requirement.changes, changes);
            issue->updatedAt = now;
            result.updated << issue->id;
            continue;
        }
        Issue issue;
        issue.id = nextId();
        issue.title = r.summary.trimmed().isEmpty() ? tr("Requerimiento %1").arg(r.id) : r.summary.simplified();
        issue.priority = priorityFromRequirement(r.priority);
        issue.requirement.connection = conn;
        issue.requirement.data = r;
        issue.requirement.importedAt = fetchedAt;
        issue.requirement.fetchedAt = fetchedAt;
        issue.createdAt = issue.updatedAt = now;
        m_issues.append(issue);
        result.created << issue.id;
    }
    if (!touched) return result;
    persist();
    for (const auto& id : result.created + result.updated) emit issueChanged(id);
    if (!result.created.isEmpty()) {
        m_selectedId = result.created.first();
        emit selectionChanged(m_selectedId);
    }
    return result;
}

QString IssueStore::openForRequirement(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt) {
    if (requirement.id.isEmpty()) return {};
    importRequirements({requirement}, connection, fetchedAt);
    const Issue* issue = findByRequirement(connection, requirement.id);
    if (!issue) return {};
    select(issue->id);   // el recién creado ya queda seleccionado; el que ya existía se trae al frente
    return issue->id;
}

IssueStore::InboxResult IssueStore::applyInbox(const QList<ExternalRequirement>& inbox, const QString& connection,
                                               const QDateTime& fetchedAt) {
    InboxResult result;
    result.missing = markInboxRead(inbox, connection, fetchedAt);
    QList<ExternalRequirement> known;
    for (const auto& r : inbox)
        if (findByRequirement(connection, r.id)) known << r;
    if (!known.isEmpty()) result.updated = importRequirements(known, connection, fetchedAt).updated;
    return result;
}

int IssueStore::markInboxRead(const QList<ExternalRequirement>& inbox, const QString& connection, const QDateTime& fetchedAt) {
    QSet<QString> present;
    for (const auto& r : inbox) present.insert(r.id);
    int missing = 0;
    bool touched = false;
    QStringList changed;
    for (auto& issue : m_issues) {
        if (!issue.isImported() || !sameConnection(issue.requirement.connection, connection)) continue;
        touched = true;
        const bool gone = !present.contains(issue.requirement.data.id);
        if (gone) ++missing;
        else issue.requirement.fetchedAt = fetchedAt;
        if (issue.requirement.missing == gone) continue;
        issue.requirement.missing = gone;
        changed << issue.id;
    }
    if (!touched) return 0;
    persist();
    for (const auto& id : changed) emit issueChanged(id);
    return missing;
}

void IssueStore::noteRequirementState(const QString& issueId, const QString& state, const QDateTime& when) {
    const Issue* issue = find(issueId);
    const QString clean = state.simplified();
    if (!issue || !issue->isImported() || clean.isEmpty()) return;
    if (issue->requirement.data.states == QStringList{clean}) return;
    updateIssue(issueId, [&clean, &when](Issue& i) {
        i.requirement.data.states = {clean};
        i.requirement.fetchedAt = when;
        // Lo que dijera un cambio pendiente sobre el estado ya no vale: éste es el de ahora.
        i.requirement.changes.removeIf([](const RequirementChange& c) { return c.field == QLatin1String("states"); });
    });
}

void IssueStore::setRequirementDetail(const QString& issueId, const RequirementDetail& detail, const QDateTime& fetchedAt) {
    const Issue* issue = find(issueId);
    if (!issue || !issue->isImported()) return;
    updateIssue(issueId, [&](Issue& i) {
        i.requirement.detail = detail;
        i.requirement.detailFetchedAt = fetchedAt;
    });
}

void IssueStore::acknowledgeChanges(const QString& issueId) {
    const Issue* issue = find(issueId);
    if (!issue || issue->requirement.changes.isEmpty()) return;
    updateIssue(issueId, [](Issue& i) { i.requirement.changes.clear(); });
}

QStringList IssueStore::caseIdsOf(const Issue& issue, const PlanStore& plans) {
    QStringList out;
    for (const auto& planId : issue.planIds)
        for (const auto& caseId : plans.orderedCaseIds(planId))
            if (!out.contains(caseId)) out << caseId;
    return out;
}

namespace {
/// Los ciclos del issue, del más reciente al primero, quedándose con los que `keep` acepte.
QList<PlanRun> cyclesWhere(const Issue& issue, const RunHistoryStore& history,
                           const std::function<bool(const PlanRun&)>& keep) {
    QList<PlanRun> cycles;
    for (const auto& cycle : history.plans()) {
        // El ciclo dice de qué issue es desde que se arranca; los anteriores a eso, y los de un plan
        // que prueba varios requerimientos, se reconocen por el plan.
        if (!issue.planIds.contains(cycle.planId) && cycle.issueId != issue.id) continue;
        if (keep && !keep(cycle)) continue;
        cycles << cycle;
    }
    std::sort(cycles.begin(), cycles.end(), [](const PlanRun& a, const PlanRun& b) {
        return a.startedAt != b.startedAt ? a.startedAt > b.startedAt : a.id > b.id;
    });
    return cycles;
}
} // namespace

QList<PlanRun> IssueStore::cyclesOf(const Issue& issue, const RunHistoryStore& history, const QDateTime& since) {
    return cyclesWhere(issue, history, [&since](const PlanRun& cycle) {
        return !since.isValid() || !cycle.startedAt.isValid() || cycle.startedAt >= since;
    });
}

QList<PlanRun> IssueStore::cyclesOfRevision(const Issue& issue, const RunHistoryStore& history, int revision) {
    if (revision <= 0) return cyclesOf(issue, history);
    // La ventana de la ronda: desde que se abrió hasta que se abrió la siguiente. Sólo hace falta para
    // los ciclos anteriores a que cada uno anotara su revisión al arrancar.
    QDateTime from, until;
    for (const auto& round : issue.revisions) {
        if (round.number == revision) from = round.startedAt;
        else if (round.number == revision + 1) until = round.startedAt;
    }
    return cyclesWhere(issue, history, [&](const PlanRun& cycle) {
        if (cycle.revision > 0) return cycle.revision == revision;
        if (!cycle.startedAt.isValid()) return true;
        if (from.isValid() && cycle.startedAt < from) return false;
        return !until.isValid() || cycle.startedAt < until;
    });
}

QList<RunRecord> IssueStore::runsOfRevision(const Issue& issue, const RunHistoryStore& history, int revision) {
    return runsOfCycles(cyclesOfRevision(issue, history, revision), history);
}

QList<RunRecord> IssueStore::runsOf(const Issue& issue, const RunHistoryStore& history, const QDateTime& since) {
    return runsOfCycles(cyclesOf(issue, history, since), history);
}

QList<RunRecord> IssueStore::runsOfCycles(const QList<PlanRun>& cycles, const RunHistoryStore& history) {
    QList<RunRecord> runs;
    for (const auto& cycle : cycles) runs += history.runsForPlan(cycle.id);
    std::sort(runs.begin(), runs.end(), [](const RunRecord& a, const RunRecord& b) {
        const QDateTime ta = a.finishedAt.isValid() ? a.finishedAt : a.startedAt;
        const QDateTime tb = b.finishedAt.isValid() ? b.finishedAt : b.startedAt;
        // Dos ejecuciones en el mismo segundo: la de id posterior es la más reciente.
        return ta != tb ? ta > tb : a.id > b.id;
    });
    return runs;
}

} // namespace qaflow
