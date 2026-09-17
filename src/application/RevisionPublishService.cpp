#include "RevisionPublishService.h"

#include "application/IssuePublishService.h"
#include "application/IssueStore.h"
#include "application/QualityRecordService.h"
#include "application/RequirementSourceService.h"
#include "application/RunHistoryStore.h"
#include "application/TestPublishService.h"

#include <memory>
#include <utility>

namespace qaflow {

/// El estado de una publicación en curso: lo elegido, lo que va pasando y a quién avisar. Vive en un
/// shared_ptr porque los pasos son asíncronos y se encadenan unos con otros.
struct RevisionPublishService::Run {
    QString issueId;
    int revision = 0;              // ronda que se está publicando (ya resuelta: nunca 0 si el issue tiene rondas)
    Options options;
    std::function<void(const Outcome&)> progress;
    std::function<void(const Result&)> done;
    Result result;
    QList<PlanReport> cycles;      // ciclos pendientes de publicar en Zephyr
    int cyclesDone = 0;
    int testsCreated = 0;
    QStringList cycleUrls;         // enlaces a los ciclos publicados, para el comentario
    QStringList problems;          // lo que Zephyr no pudo publicar
};

RevisionPublishService::RevisionPublishService(IssueStore& issues, RunHistoryStore& history, QualityRecordService& records,
                                               TestPublishService* zephyr, IssuePublishService* tracker,
                                               RequirementSourceService* requirements, QObject* parent)
    : QObject(parent), m_issues(issues), m_history(history), m_records(records), m_zephyr(zephyr), m_tracker(tracker),
      m_requirements(requirements) {}

QString RevisionPublishService::label(Destination destination) {
    switch (destination) {
        case Destination::Zephyr: return tr("Zephyr");
        case Destination::Tracker: return tr("el gestor");
        case Destination::Requirement: return tr("GESREQ");
    }
    return {};
}

QList<PlanReport> RevisionPublishService::cyclesFor(const QString& issueId, int revision) const {
    return m_records.cyclesFor(issueId, revision);
}

RequirementRegistration RevisionPublishService::registrationFor(const QString& issueId, QaOutcome outcome,
                                                               const QString& documentPath, int revision) const {
    const Issue* issue = m_issues.find(issueId);
    RequirementRegistration registration;
    if (!issue || !issue->isImported()) return registration;
    registration.requirementId = issue->requirement.data.id;
    registration.systemCode = issue->requirement.data.systemCode;
    registration.result = toString(outcome);
    registration.attachmentPath = documentPath;
    // El resumen de observaciones que GESREQ pide es el mismo que cuenta el acta, así que los dos
    // documentos dicen lo mismo.
    const IssueRevision* round = issue->revision(revision);
    registration.observations = round && !round->record.isEmpty() ? round->record.observations
                                                                  : m_records.draftFor(issueId, QString(), revision).observations;
    return registration;
}

QString RevisionPublishService::alreadyRegistered(const Issue& issue, int revision) const {
    // Registrar cambia el estado del requerimiento en GESREQ y lo saca de la bandeja de control: una
    // ronda ya registrada no se vuelve a registrar, y un control cerrado como Conforme, tampoco. Volver
    // a registrar sólo tiene sentido cuando la ronda anterior quedó **observada** y se probó otra vez.
    const IssueRevision* round = issue.revision(revision);
    const int number = round ? round->number : 0;
    if (round && round->gesreq.registeredAt.isValid())
        return tr("El resultado de esta revisión ya se registró en GESREQ el %1 como %2")
                .arg(round->gesreq.registeredAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")),
                     qaflow::label(round->gesreq.result));
    const IssueRevision* registered = nullptr;
    for (const auto& other : issue.revisions)
        if (other.gesreq.registeredAt.isValid()) registered = &other;
    if (!registered) return {};
    const QString when = registered->gesreq.registeredAt.toString(QStringLiteral("dd/MM/yyyy HH:mm"));
    // Una ronda posterior ya dijo la última palabra sobre el requerimiento: registrar ahora la de antes
    // lo dejaría en un estado que ya no es el suyo.
    if (registered->number > number)
        return tr("La revisión %1 ya se registró en GESREQ el %2: lo de esta ronda ya no es lo último que sabe el sistema")
                .arg(registered->number)
                .arg(when);
    if (registered->gesreq.result == QaOutcome::Conforme)
        return tr("El control ya se registró como Conforme el %1: el requerimiento salió de tu bandeja").arg(when);
    return {};
}

QString RevisionPublishService::requirementProblem(const QString& issueId, QaOutcome outcome,
                                                   const QString& documentPath, int revision) const {
    if (!m_requirements) return {};
    return m_requirements->registrationProblem(registrationFor(issueId, outcome, documentPath, revision));
}

QList<RevisionPublishService::Destination> RevisionPublishService::pendingFor(const QString& issueId, int revision) const {
    QList<Destination> pending;
    for (const auto& step : stepsFor(issueId, revision))
        if (!step.done && step.available) pending << step.destination;
    return pending;
}

QList<RevisionPublishService::Step> RevisionPublishService::stepsFor(const QString& issueId, int round) const {
    const Issue* issue = m_issues.find(issueId);
    if (!issue) return {};
    const IssueRevision* revision = issue->revision(round);
    const QList<PlanReport> cycles = cyclesFor(issueId, round);

    Step zephyr;
    zephyr.destination = Destination::Zephyr;
    int published = 0, executed = 0;
    for (const auto& cycle : cycles) {
        if (cycle.plan.isPublished()) ++published;
        executed += cycle.executed;
    }
    zephyr.target = tr("Zephyr · %1 ciclo(s) de los planes del issue").arg(cycles.size());
    zephyr.done = !cycles.isEmpty() && published == cycles.size();
    zephyr.available = m_zephyr && m_zephyr->enabled() && executed > 0;
    if (!m_zephyr || !m_zephyr->enabled()) zephyr.blocked = tr("Activa Zephyr en Ajustes para publicar los ciclos");
    else if (cycles.isEmpty()) zephyr.blocked = tr("La revisión no tiene ningún ciclo de plan ejecutado");
    else if (executed == 0) zephyr.blocked = tr("Ningún caso de esos ciclos llegó a ejecutarse");
    zephyr.detail = zephyr.done ? tr("Ya publicados: se actualizan sus ejecuciones y evidencias")
                                : tr("%1 caso(s) ejecutado(s); los ciclos ya publicados se actualizan").arg(executed);

    Step tracker;
    tracker.destination = Destination::Tracker;
    tracker.target = issue->isPublished() ? tr("%1 · %2").arg(issue->publication.tracker, issue->publication.key)
                                          : tr("El issue no está en el gestor");
    tracker.available = m_tracker && m_tracker->canPublishResult(*issue);
    tracker.done = revision && !revision->jira.isEmpty() && !revision->jira.uncertain && revision->jira.publishedAt.isValid();
    if (!tracker.available)
        tracker.blocked = issue->isPublished()
                              ? tr("El gestor configurado no admite comentarios")
                              : tr("El issue no llegó a crearse en el gestor al importarlo: publícalo desde «Publicación en el gestor»");
    tracker.detail = tracker.done ? tr("El resultado ya se comentó el %1").arg(revision->jira.publishedAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")))
                                  : tr("Comentario con el resultado, los enlaces de Zephyr y el acta adjunta");

    Step requirement;
    requirement.destination = Destination::Requirement;
    requirement.target = issue->isImported() ? tr("GESREQ · GREQ %1").arg(issue->requirement.data.id)
                                             : tr("El issue no viene de GESREQ");
    requirement.available = issue->isImported() && m_requirements && m_requirements->canRegisterResult();
    requirement.done = revision && !revision->gesreq.isEmpty() && !revision->gesreq.uncertain && revision->gesreq.registeredAt.isValid();
    if (!requirement.available)
        requirement.blocked = issue->isImported() ? tr("Esta versión no registra resultados en GESREQ: hazlo en el sistema")
                                                  : tr("Sólo se registra el resultado de un requerimiento importado");
    if (requirement.available)
        if (const QString done = alreadyRegistered(*issue, round); !done.isEmpty()) {
            requirement.available = false;
            requirement.blocked = done;
        }
    // Lo que GESREQ no aceptaría se dice aquí, antes de enviar nada: el acta que falta o el resultado
    // que no cuadra con las observaciones del acta.
    if (requirement.available) {
        const QaOutcome proposed = revision && revision->outcome != QaOutcome::Pendiente ? revision->outcome : QaOutcome::Observado;
        const QString problem = requirementProblem(issueId, proposed, revision ? revision->documentPath : QString(), round);
        if (!problem.isEmpty()) {
            requirement.available = false;
            requirement.blocked = problem;
            requirement.rule = true;
        }
    }
    if (requirement.done) {
        requirement.detail = tr("Registrado el %1 como %2").arg(revision->gesreq.registeredAt.toString(QStringLiteral("dd/MM/yyyy HH:mm")),
                                                                qaflow::label(revision->gesreq.result));
        if (!revision->gesreq.requirementState.isEmpty())
            requirement.detail += tr(" · el requerimiento quedó en «%1»").arg(revision->gesreq.requirementState);
    } else {
        requirement.detail = tr("Cambia el estado del requerimiento en GESREQ");
    }

    return {zephyr, tracker, requirement};
}

void RevisionPublishService::publish(const QString& issueId, const Options& options,
                                     std::function<void(const Outcome&)> progress, std::function<void(const Result&)> done) {
    const Issue* issue = m_issues.find(issueId);
    const IssueRevision* round = issue ? issue->revision(options.revision) : nullptr;
    auto run = std::make_shared<Run>();
    run->issueId = issueId;
    // La ronda se resuelve una sola vez, al empezar: los pasos son asíncronos y entre uno y otro puede
    // abrirse otra (nadie está mirando), y lo que se publica tiene que seguir siendo lo mismo.
    run->revision = round ? round->number : 0;
    run->options = options;
    run->progress = std::move(progress);
    run->done = std::move(done);
    run->result.ok = true;   // lo baja el primer paso que no salga
    if (options.zephyr) run->cycles = cyclesFor(issueId, run->revision);
    runZephyr(run);
}

void RevisionPublishService::finish(const std::shared_ptr<Run>& run, const Outcome& outcome) {
    run->result.steps << outcome;
    if (!outcome.ok) run->result.ok = false;
    if (run->progress) run->progress(outcome);
}

void RevisionPublishService::runZephyr(const std::shared_ptr<Run>& run) {
    if (!run->options.zephyr || !m_zephyr || !m_zephyr->enabled() || run->cycles.isEmpty()) {
        if (run->options.zephyr) {
            Outcome outcome;
            outcome.destination = Destination::Zephyr;
            outcome.message = tr("No hay ciclos que publicar en Zephyr");
            finish(run, outcome);
        }
        runTracker(run);
        return;
    }

    const PlanReport cycle = run->cycles.takeFirst();
    const bool update = cycle.plan.isPublished();
    auto next = [this, run, cycle, update](const PublishResult& r) {
        if (r.ok) {
            ++run->cyclesDone;
            run->testsCreated += r.testsCreated;
            // El enlace sale del ciclo ya marcado como publicado: es el que verá quien lea el comentario.
            const QString url = m_zephyr->cycleUrl(m_history.report(cycle.plan.id));
            if (!url.isEmpty() && !run->cycleUrls.contains(url)) run->cycleUrls << url;
        } else {
            run->problems << tr("%1: %2").arg(cycle.plan.name, r.error);
        }
        for (const auto& skipped : r.skipped) run->problems << skipped;

        if (!run->cycles.isEmpty()) {
            runZephyr(run);
            return;
        }
        Outcome outcome;
        outcome.destination = Destination::Zephyr;
        outcome.ok = run->cyclesDone > 0 && run->problems.isEmpty();
        outcome.message = run->cyclesDone > 0
                              ? tr("%1 ciclo(s) en Zephyr · %2 Test(s) creado(s)").arg(run->cyclesDone).arg(run->testsCreated)
                              : tr("No se pudo publicar en Zephyr");
        if (!run->problems.isEmpty()) outcome.message += QStringLiteral("\n") + run->problems.join(QLatin1Char('\n'));
        finish(run, outcome);
        runTracker(run);
    };
    if (update) m_zephyr->update(cycle, next);
    else m_zephyr->publish(cycle, next);
}

void RevisionPublishService::runTracker(const std::shared_ptr<Run>& run) {
    const Issue* issue = m_issues.find(run->issueId);
    if (!run->options.tracker || !issue || !m_tracker || !m_tracker->canPublishResult(*issue)) {
        if (run->options.tracker) {
            Outcome outcome;
            outcome.destination = Destination::Tracker;
            outcome.message = tr("No se puede dejar el resultado en el gestor");
            finish(run, outcome);
        }
        runRequirement(run);
        return;
    }

    // Lo publicado en Zephyr se cuenta en el comentario: quien lo lea llega desde ahí a las pruebas.
    QString comment = run->options.comment;
    for (const auto& url : run->cycleUrls)
        if (!comment.contains(url)) comment += QStringLiteral("\n") + tr("Ciclo en Zephyr: %1").arg(url);

    m_tracker->publishResult(run->issueId, comment, run->options.documentPath,
                             [this, run](const IssuePublishService::Result& r) {
                                 if (r.ok) {
                                     // Publicado el resultado, el issue se pone al día con lo que dice
                                     // el gestor: su estado allí puede haber cambiado mientras se probaba.
                                     m_tracker->refreshStatus(run->issueId, [](const IssuePublishService::Result&) {});
                                     linkEvidence(run, r.key);
                                     return;
                                 }
                                 Outcome outcome;
                                 outcome.destination = Destination::Tracker;
                                 outcome.uncertain = r.uncertain;
                                 outcome.message = r.uncertain
                                                           ? tr("El envío se cortó sin respuesta: compruébalo en el gestor · %1").arg(r.error)
                                                           : tr("No se pudo comentar en el gestor · %1").arg(r.error);
                                 finish(run, outcome);
                                 runRequirement(run);
                             },
                             run->revision);
}

void RevisionPublishService::linkEvidence(const std::shared_ptr<Run>& run, const QString& key) {
    const Issue* issue = m_issues.find(run->issueId);
    QStringList bugs;
    if (issue)
        for (const auto& bug : m_records.revisionBugs(*issue, run->revision))
            if (!bug.key.trimmed().isEmpty()) bugs << bug.key.trimmed();
    // Los Tests salen de los ciclos ya publicados: cada ejecución guarda el suyo al pasar por Zephyr.
    QStringList tests;
    for (const auto& cycle : cyclesFor(run->issueId, run->revision))
        for (const auto& row : cycle.rows)
            if (!row.testKey.trimmed().isEmpty() && !tests.contains(row.testKey.trimmed())) tests << row.testKey.trimmed();

    m_tracker->linkToIssue(run->issueId, bugs + tests, [this, run, key, bugs, tests](const IssuePublishService::LinkResult& links) {
        Outcome outcome;
        outcome.destination = Destination::Tracker;
        outcome.ok = true;
        outcome.message = tr("Resultado comentado en %1").arg(key);
        if (links.linked > 0)
            outcome.message += tr(" · %1 enlace(s) al issue: %2 bug(s) y %3 Test(s)").arg(links.linked).arg(bugs.size()).arg(tests.size());
        // Un enlace que no se pudo crear no invalida el comentario: se dice y se sigue.
        if (!links.failed.isEmpty())
            outcome.message += QStringLiteral("\n") + tr("Sin enlazar: %1").arg(links.failed.join(QStringLiteral(" · ")));
        finish(run, outcome);
        runRequirement(run);
    });
}

void RevisionPublishService::runRequirement(const std::shared_ptr<Run>& run) {
    const Issue* issue = m_issues.find(run->issueId);
    const QString registeredAlready = issue ? alreadyRegistered(*issue, run->revision) : QString();
    const bool possible = run->options.requirement && issue && issue->isImported() && m_requirements &&
                          m_requirements->canRegisterResult() && registeredAlready.isEmpty();
    if (!possible) {
        if (run->options.requirement) {
            Outcome outcome;
            outcome.destination = Destination::Requirement;
            // Registrar dos veces la misma ronda cambiaría otra vez el estado del requerimiento.
            outcome.message = registeredAlready.isEmpty() ? tr("No se puede registrar el resultado en GESREQ") : registeredAlready;
            finish(run, outcome);
        }
        if (run->done) run->done(run->result);
        return;
    }

    // Sólo se cierra sola la ronda que se está probando: terminar de publicar una anterior no toca su cierre.
    const IssueRevision* open = issue->currentRevision();
    const bool wasOpen = open != nullptr && open->number == run->revision;
    const QaOutcome outcome = run->options.outcome;
    RequirementRegistration registration = registrationFor(run->issueId, outcome, run->options.documentPath, run->revision);
    registration.comment = run->options.comment;
    const bool withDocument = !registration.attachmentPath.isEmpty();

    m_requirements->registerResult(registration, [this, run, outcome, wasOpen, withDocument](const RequirementRegistrationResult& r) {
        RevisionRegistration registered;
        registered.result = outcome;
        registered.comment = run->options.comment;
        registered.requirementState = r.state;
        if (r.ok) {
            registered.registeredAt = QDateTime::currentDateTime();
            registered.attachedDocument = withDocument;
        } else {
            registered.uncertain = r.uncertain;
            registered.lastError = r.error;
        }
        m_issues.setRevisionRegistration(run->issueId, registered, run->revision);
        // Registrado el resultado, la ronda queda cerrada con él: observado, volver a probar abre la siguiente.
        if (r.ok && wasOpen) m_issues.closeRevision(run->issueId, outcome);
        // Y el issue se actualiza con el estado en el que GESREQ deja el requerimiento, que lo dice al
        // guardar: así la pantalla enseña lo que hay en el sistema sin volver a consultar la bandeja.
        if (r.ok) m_issues.noteRequirementState(run->issueId, r.state);

        Outcome step;
        step.destination = Destination::Requirement;
        step.ok = r.ok;
        step.uncertain = r.uncertain;
        step.message = r.ok ? (r.state.isEmpty()
                                   ? tr("Registrado en GESREQ como %1").arg(qaflow::label(outcome))
                                   : tr("Registrado en GESREQ como %1 · el requerimiento quedó en «%2»")
                                         .arg(qaflow::label(outcome), r.state))
                            : (r.uncertain ? tr("El registro no quedó confirmado: compruébalo en GESREQ antes de repetirlo · %1").arg(r.error)
                                           : tr("No se pudo registrar en GESREQ · %1").arg(r.error));
        finish(run, step);
        if (run->done) run->done(run->result);
    });
}

} // namespace qaflow
