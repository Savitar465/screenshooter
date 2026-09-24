// RevisionPublishService (application/RevisionPublishService.h): publicar el resultado de una revisión
// terminada en sus tres destinos —los ciclos de los planes en Zephyr, el resultado y el acta en el
// gestor y el registro en GESREQ—, qué se puede hacer en cada uno y qué pasa cuando alguno falla.

#include "support/AppFixture.h"

#include "application/RevisionPublishService.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;

namespace {

const QString kConnection = QStringLiteral("http://gesreq.test:7401/greq");
using Destination = RevisionPublishService::Destination;

ExternalRequirement requirement() {
    ExternalRequirement r;
    r.id = QStringLiteral("2026997");
    r.system = QStringLiteral("SUMA2-INGRESO");
    r.systemCode = QStringLiteral("SUMA2");
    r.summary = QStringLiteral("Integración de nuevos servicios");
    r.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
    return r;
}

/// Un issue importado, publicado en el gestor (salvo que se pida lo contrario), con su plan ejecutado
/// y su revisión cerrada: justo lo que hay cuando se va a publicar el resultado.
struct Finished {
    QString issueId;
    QString planId;
    QString planRunId;
};

Finished finishedRevision(AppFixture& f, bool publishedInTracker = true) {
    f.settings.updateRequirementSource([](RequirementSourceSettings& r) {
        r.url = kConnection;
        r.user = QStringLiteral("jmaidana");
        r.password = QStringLiteral("secreto");
        r.connected = true;
    });
    f.settings.updateTracker([](TrackerSettings& t) {
        t.zephyr = true;
        t.url = QStringLiteral("https://acme.atlassian.net");
        t.project = QStringLiteral("SHOP");
    });
    f.issues.importRequirements({requirement()}, kConnection);
    const QString id = f.issues.issues().first().id;
    if (publishedInTracker)
        f.issues.updateIssue(id, [](Issue& i) {
            i.publication.tracker = QStringLiteral("Jira");
            i.publication.key = QStringLiteral("SHOP-12");
            i.publication.url = QStringLiteral("https://acme.atlassian.net/browse/SHOP-12");
            i.publication.publishedAt = QDateTime::currentDateTime();
        });
    const QString planId = f.plans.createPlan(QStringLiteral("Plan GREQ 2026997"));
    f.plans.toggle(QStringLiteral("TC-101"));
    f.issues.linkPlan(id, planId);
    const QString planRunId = f.history.startPlan(QStringLiteral("Plan GREQ 2026997"), {QStringLiteral("TC-101")}, planId);
    // Como en la aplicación: al arrancar, el ciclo anota de qué issue y de qué ronda es.
    const IssueStore::RevisionRef started = f.issues.notePlanStarted(planId);
    f.history.noteCycleRevision(planRunId, started.issueId, started.revision);
    RunRecord run;
    run.caseId = QStringLiteral("TC-101");
    run.caseTitle = QStringLiteral("Autenticación");
    run.planRunId = planRunId;
    run.verdict = Verdict::Fallido;
    run.startedAt = QDateTime::currentDateTime();
    run.finishedAt = run.startedAt.addSecs(300);
    f.history.addRun(run);
    f.history.finishPlan(planRunId);
    f.issues.closeRevision(id, QaOutcome::Observado);
    return {id, planId, planRunId};
}

/// Vuelve a probar el requerimiento: abre la ronda siguiente y ejecuta otro ciclo del mismo plan, que
/// es lo que pasa cuando un control observado se corrige.
QString anotherRound(AppFixture& f, const Finished& done) {
    f.issues.openRevision(done.issueId);
    const QString planRunId = f.history.startPlan(QStringLiteral("Plan GREQ 2026997"), {QStringLiteral("TC-101")}, done.planId);
    const IssueStore::RevisionRef started = f.issues.notePlanStarted(done.planId);
    f.history.noteCycleRevision(planRunId, started.issueId, started.revision);
    RunRecord run;
    run.caseId = QStringLiteral("TC-101");
    run.caseTitle = QStringLiteral("Autenticación");
    run.planRunId = planRunId;
    run.verdict = Verdict::Superado;
    run.startedAt = QDateTime::currentDateTime();
    run.finishedAt = run.startedAt.addSecs(120);
    f.history.addRun(run);
    f.history.finishPlan(planRunId);
    return planRunId;
}

/// Un proyecto de una sola fase: el Conforme de su ronda es el final, el que se registra en GESREQ y
/// cierra el issue del gestor. Las pruebas que no van de fases usan esto para hablar sólo de publicar.
void singlePhase(AppFixture& f) { f.issues.setPhases({QStringLiteral("QA")}); }

RevisionPublishService::Step stepOf(const QList<RevisionPublishService::Step>& steps, Destination destination) {
    for (const auto& step : steps)
        if (step.destination == destination) return step;
    return {};
}

} // namespace

class RevisionPublishServiceTest : public QObject {
    Q_OBJECT
private slots:
    void itSaysWhatCanBePublishedAndWhereItGoes() {
        AppFixture f;
        const Finished done = finishedRevision(f);
        const QList<RevisionPublishService::Step> steps = f.revisionPublish.stepsFor(done.issueId);
        QCOMPARE(steps.size(), 4);

        const auto zephyr = stepOf(steps, Destination::Zephyr);
        QVERIFY(zephyr.available);
        QVERIFY(!zephyr.done);
        QVERIFY2(zephyr.target.contains(QStringLiteral("GREQ 2026997 · QA")), qPrintable(zephyr.target));   // el ciclo de la fase

        const auto tracker = stepOf(steps, Destination::Tracker);
        QVERIFY(tracker.available);
        QVERIFY(tracker.target.contains(QStringLiteral("SHOP-12")));

        const auto gesreq = stepOf(steps, Destination::Requirement);
        QVERIFY(gesreq.available);
        QVERIFY(gesreq.target.contains(QStringLiteral("2026997")));

        const auto close = stepOf(steps, Destination::Close);
        QVERIFY(close.available);
        QVERIFY(!close.done);
    }

    // Publicada como Conforme y con todo bien, el issue del gestor se cierra al final.
    void aConformePublicationClosesTheTrackerIssueAtTheEnd() {
        AppFixture f;
        singlePhase(f);
        const Finished done = finishedRevision(f);
        f.tracker->resolvedToReturn = true;
        f.tracker->statusToReturn = QStringLiteral("Cerrada");
        RevisionPublishService::Options options;
        options.outcome = QaOutcome::Conforme;
        options.comment = QStringLiteral("Conforme");

        QList<Destination> order;
        RevisionPublishService::Result result;
        f.revisionPublish.publish(
            done.issueId, options, [&order](const RevisionPublishService::Outcome& o) { order << o.destination; },
            [&result](const RevisionPublishService::Result& r) { result = r; });
        QVERIFY2(result.ok, qPrintable(result.steps.isEmpty() ? QString() : result.steps.last().message));
        QCOMPARE(order.last(), Destination::Close);
        QCOMPARE(f.tracker->closed, QStringList{QStringLiteral("SHOP-12")});
        QVERIFY(f.issues.find(done.issueId)->publication.resolved);
        QVERIFY(result.steps.last().message.contains(QStringLiteral("Cerrada")));
        QVERIFY(stepOf(f.revisionPublish.stepsFor(done.issueId), Destination::Close).done);
    }

    // Observado no cierra nada, y si algo de lo elegido falla, el issue sigue abierto.
    void theTrackerIssueStaysOpenWhenObservedOrWhenSomethingFailed() {
        AppFixture f;
        singlePhase(f);
        const Finished done = finishedRevision(f);
        RevisionPublishService::Options options;
        options.outcome = QaOutcome::Observado;
        options.comment = QStringLiteral("Observado");
        RevisionPublishService::Result result;
        f.revisionPublish.publish(done.issueId, options, {}, [&result](const RevisionPublishService::Result& r) { result = r; });
        QVERIFY(result.ok);
        QVERIFY(f.tracker->closed.isEmpty());
        for (const auto& step : result.steps) QVERIFY(step.destination != Destination::Close);

        AppFixture g;
        singlePhase(g);
        const Finished other = finishedRevision(g);
        g.requirementSource->registrationCutOff = true;   // GESREQ no confirma
        options.outcome = QaOutcome::Conforme;
        g.revisionPublish.publish(other.issueId, options, {}, [&result](const RevisionPublishService::Result& r) { result = r; });
        QVERIFY(!result.ok);
        QVERIFY(g.tracker->closed.isEmpty());
        QCOMPARE(result.steps.last().destination, Destination::Close);
        QVERIFY(!result.steps.last().ok);
    }

    // QA aprobada no es el OK del requerimiento: ni se registra en GESREQ ni cierra el issue del gestor.
    // Eso lo hace el Conforme de PRE, la última fase.
    void onlyTheLastPhaseRegistersTheOkAndClosesTheTrackerIssue() {
        AppFixture f;
        const Finished done = finishedRevision(f);   // revisión 1, QA observada
        anotherRound(f, done);                         // revisión 2, QA
        QCOMPARE(f.revisionPublish.phaseFor(done.issueId), QStringLiteral("QA"));
        QVERIFY(!f.revisionPublish.closesRequirement(done.issueId, QaOutcome::Conforme));
        f.issues.closeRevision(done.issueId, QaOutcome::Conforme);

        const auto requirement = stepOf(f.revisionPublish.stepsFor(done.issueId), Destination::Requirement);
        QVERIFY(!requirement.available);
        QVERIFY2(requirement.blocked.contains(QStringLiteral("Aprobada en QA")), qPrintable(requirement.blocked));
        QVERIFY(!f.revisionPublish.pendingFor(done.issueId, 2).contains(Destination::Requirement));
        QVERIFY(!f.revisionPublish.pendingFor(done.issueId, 2).contains(Destination::Close));

        RevisionPublishService::Options options;
        options.outcome = QaOutcome::Conforme;
        options.comment = QStringLiteral("QA aprobada");
        RevisionPublishService::Result result;
        f.revisionPublish.publish(done.issueId, options, {}, [&result](const RevisionPublishService::Result& r) { result = r; });
        QVERIFY(f.requirementSource->registrations.isEmpty());
        QVERIFY(f.tracker->closed.isEmpty());

        // PRE: la ronda siguiente es de la última fase, y su Conforme registra el OK y cierra.
        anotherRound(f, done);
        QCOMPARE(f.revisionPublish.phaseFor(done.issueId), QStringLiteral("PRE"));
        QVERIFY(f.revisionPublish.closesRequirement(done.issueId, QaOutcome::Conforme));
        f.tracker->resolvedToReturn = true;
        options.comment = QStringLiteral("Conforme");
        options.tracker = false;
        f.revisionPublish.publish(done.issueId, options, {}, [&result](const RevisionPublishService::Result& r) { result = r; });
        QVERIFY2(result.ok, qPrintable(result.steps.isEmpty() ? QString() : result.steps.last().message));
        QCOMPARE(f.requirementSource->registrations.size(), 1);
        QCOMPARE(f.tracker->closed, QStringList{QStringLiteral("SHOP-12")});
        QVERIFY(f.issues.find(done.issueId)->state == IssueState::Done);
    }

    // Los ciclos de plan de una fase van todos al mismo ciclo de Zephyr, del más antiguo al más
    // reciente: el primero lo crea y los siguientes actualizan sus ejecuciones.
    void theCyclesOfAPhaseGoToTheSameZephyrCycleInOrder() {
        AppFixture f;
        const Finished done = finishedRevision(f);
        // Otro ciclo del plan en la misma ronda, posterior al primero.
        const QString later = f.history.startPlan(QStringLiteral("Plan GREQ 2026997"), {QStringLiteral("TC-101")}, done.planId,
                                                  QStringLiteral("QA"));
        f.history.noteCycleRevision(later, done.issueId, 1);
        RunRecord run;
        run.caseId = QStringLiteral("TC-101");
        run.planRunId = later;
        run.verdict = Verdict::Superado;
        run.startedAt = QDateTime::currentDateTime().addSecs(60);
        run.finishedAt = run.startedAt.addSecs(60);
        f.history.addRun(run);
        f.history.finishPlan(later);

        RevisionPublishService::Options options;
        options.outcome = QaOutcome::Observado;
        options.tracker = false;
        options.requirement = false;
        options.close = false;
        RevisionPublishService::Result result;
        f.revisionPublish.publish(done.issueId, options, {}, [&result](const RevisionPublishService::Result& r) { result = r; });
        QCOMPARE(f.zephyr->published.size(), 2);
        QCOMPARE(f.zephyr->published[0].cycleName, QStringLiteral("GREQ 2026997 · QA"));
        QVERIFY(f.zephyr->published[0].cycleId.isEmpty());                    // el primero crea el ciclo de la fase
        QCOMPARE(f.zephyr->published[1].cycleId, QStringLiteral("77"));       // el siguiente lo actualiza
        QVERIFY(f.zephyr->published[1].cases.first().verdict == Verdict::Superado);   // y queda lo último
        QCOMPARE(f.issues.find(done.issueId)->zephyr.cycleOf(QStringLiteral("QA")), QStringLiteral("77"));
    }

    // Los Tests del requerimiento se crean antes de probarlo y se enlazan a su issue del gestor.
    void theTestsOfTheRequirementAreCreatedAndLinkedToItsIssue() {
        AppFixture f;
        const Finished done = finishedRevision(f);
        QVERIFY(f.revisionPublish.canPrepareTests());
        const QStringList cases{QStringLiteral("TC-101")};
        QCOMPARE(f.revisionPublish.casesWithoutTest(done.issueId, cases), cases);
        RevisionPublishService::TestsPrepared out;
        f.revisionPublish.prepareTests(done.issueId, cases, [&out](const RevisionPublishService::TestsPrepared& r) { out = r; });
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.created, 1);
        QCOMPARE(out.linked, 1);
        const QList<QPair<QString, QString>> expected{{QStringLiteral("SHOP-101"), QStringLiteral("SHOP-12")}};
        QCOMPARE(f.tracker->links, expected);
        QVERIFY(f.revisionPublish.casesWithoutTest(done.issueId, cases).isEmpty());
    }

    void publishingSendsTheCycleTheCommentAndTheRegistrationInOrder() {
        AppFixture f;
        const Finished done = finishedRevision(f);
        RevisionPublishService::Options options;
        options.outcome = QaOutcome::Observado;
        options.comment = QStringLiteral("Control de calidad GREQ 2026997 — revisión 1: Observado");
        options.documentPath = QStringLiteral("/tmp/acta.docx");

        QList<Destination> order;
        RevisionPublishService::Result result;
        f.revisionPublish.publish(
            done.issueId, options, [&order](const RevisionPublishService::Outcome& o) { order << o.destination; },
            [&result](const RevisionPublishService::Result& r) { result = r; });

        QVERIFY(result.ok);
        QCOMPARE(order, QList<Destination>({Destination::Zephyr, Destination::Tracker, Destination::Requirement}));

        // Zephyr: el ciclo del plan del issue, con su caso ejecutado, y el ciclo queda apuntado.
        QCOMPARE(f.zephyr->published.size(), 1);
        QCOMPARE(f.zephyr->published.first().cases.size(), 1);
        QCOMPARE(f.zephyr->published.first().cases.first().caseId, QStringLiteral("TC-101"));
        QVERIFY(f.history.findPlan(done.planRunId)->isPublished());

        // El gestor: un comentario en el issue publicado, con el acta adjunta y el enlace del ciclo.
        QCOMPARE(f.tracker->commentedKeys, QStringList{QStringLiteral("SHOP-12")});
        QVERIFY(f.tracker->comments.first().contains(QStringLiteral("revisión 1: Observado")));
        QVERIFY(f.tracker->comments.first().contains(QStringLiteral("Ciclo en Zephyr")));
        QCOMPARE(f.tracker->commentAttachments.first(), QStringList{QStringLiteral("/tmp/acta.docx")});
        QVERIFY(!f.issues.find(done.issueId)->revisions.last().jira.isEmpty());

        // GESREQ: el resultado con el resumen de observaciones del acta y el acta adjunta.
        QCOMPARE(f.requirementSource->registrations.size(), 1);
        const RequirementRegistration& registration = f.requirementSource->registrations.first();
        QCOMPARE(registration.requirementId, QStringLiteral("2026997"));
        QCOMPARE(registration.systemCode, QStringLiteral("SUMA2"));
        QCOMPARE(registration.result, QStringLiteral("Observado"));
        QCOMPARE(registration.observations.size(), 5);
        QCOMPARE(registration.attachmentPath, QStringLiteral("/tmp/acta.docx"));
        const IssueRevision& revision = f.issues.find(done.issueId)->revisions.last();
        QVERIFY(revision.gesreq.registeredAt.isValid());
        QVERIFY(revision.gesreq.result == QaOutcome::Observado);
    }

    // El issue se crea en el gestor al importar el requerimiento, no aquí: sin él, el paso lo dice y
    // manda a su tarjeta.
    void withoutTheIssueInTheTrackerTheStepSaysWhereToPublishIt() {
        AppFixture f;
        const Finished done = finishedRevision(f, false);
        const auto tracker = stepOf(f.revisionPublish.stepsFor(done.issueId), Destination::Tracker);
        QVERIFY(!tracker.available);
        QVERIFY(tracker.blocked.contains(QStringLiteral("Publicación en el gestor")));

        RevisionPublishService::Options options;
        options.zephyr = false;
        options.requirement = false;
        options.outcome = QaOutcome::Observado;
        RevisionPublishService::Result result;
        f.revisionPublish.publish(done.issueId, options, {}, [&result](const RevisionPublishService::Result& r) { result = r; });
        QVERIFY(!result.ok);
        QVERIFY(f.tracker->publishedIssues.isEmpty());   // publicar el resultado nunca crea el issue
        QVERIFY(f.tracker->comments.isEmpty());
    }

    // Registrar cambia el estado del requerimiento en GESREQ: el issue se queda con el que devuelve el
    // sistema, y esa ronda ya no se vuelve a registrar.
    void registeringUpdatesTheRequirementAndIsNotRepeated() {
        AppFixture f;
        const Finished done = finishedRevision(f);
        RevisionPublishService::Options options;
        options.zephyr = false;
        options.tracker = false;
        options.outcome = QaOutcome::Observado;
        options.comment = QStringLiteral("Observado");
        f.revisionPublish.publish(done.issueId, options, {}, {});

        const Issue* issue = f.issues.find(done.issueId);
        QCOMPARE(issue->requirement.data.states, QStringList{QStringLiteral("CONTROL DE CALIDAD OBSERVADO")});
        QCOMPARE(issue->revisions.last().gesreq.requirementState, QStringLiteral("CONTROL DE CALIDAD OBSERVADO"));

        const auto step = stepOf(f.revisionPublish.stepsFor(done.issueId), Destination::Requirement);
        QVERIFY(step.done);
        QVERIFY(!step.available);
        QVERIFY2(step.blocked.contains(QStringLiteral("ya se registró")), qPrintable(step.blocked));

        // Y si se pide igualmente, no se manda un segundo registro.
        RevisionPublishService::Result again;
        f.revisionPublish.publish(done.issueId, options, {}, [&again](const RevisionPublishService::Result& r) { again = r; });
        QVERIFY(!again.ok);
        QCOMPARE(f.requirementSource->registrations.size(), 1);
    }

    // Observado vuelve a pruebas: la revisión siguiente sí se registra. Conforme cierra el control.
    void onlyAnObservedControlCanBeRegisteredAgain() {
        AppFixture f;
        singlePhase(f);
        const Finished done = finishedRevision(f);
        RevisionPublishService::Options options;
        options.zephyr = false;
        options.tracker = false;
        options.outcome = QaOutcome::Observado;
        f.revisionPublish.publish(done.issueId, options, {}, {});

        // Se vuelve a probar: la ronda siguiente puede registrarse.
        f.issues.openRevision(done.issueId);
        f.issues.closeRevision(done.issueId, QaOutcome::Conforme);
        auto step = stepOf(f.revisionPublish.stepsFor(done.issueId), Destination::Requirement);
        QVERIFY2(step.available, qPrintable(step.blocked));

        options.outcome = QaOutcome::Conforme;
        f.revisionPublish.publish(done.issueId, options, {}, {});
        QCOMPARE(f.requirementSource->registrations.size(), 2);
        QCOMPARE(f.issues.find(done.issueId)->requirement.data.states, QStringList{QStringLiteral("CONTROL DE CALIDAD REALIZADO")});

        // Conforme cierra el control: ni en esta ronda ni en otra nueva se vuelve a registrar.
        f.issues.openRevision(done.issueId);
        f.issues.closeRevision(done.issueId, QaOutcome::Observado);
        step = stepOf(f.revisionPublish.stepsFor(done.issueId), Destination::Requirement);
        QVERIFY(!step.available);
        QVERIFY2(step.blocked.contains(QStringLiteral("Conforme")), qPrintable(step.blocked));
    }

    // Una ronda que se quedó sin publicar (se volvió a probar antes de mandarla) se publica por su
    // número: lo suyo va a su revisión y la ronda en curso no se toca.
    void aRevisionLeftHalfwayIsPublishedByItsNumber() {
        AppFixture f;
        const Finished done = finishedRevision(f);
        const QString secondCycle = anotherRound(f, done);
        const Issue* issue = f.issues.find(done.issueId);
        QCOMPARE(issue->revisions.size(), 2);
        QVERIFY(issue->currentRevision() != nullptr);   // la ronda 2 está abierta

        // Cada ronda habla de sus ciclos: la 1, del suyo; la 2, del nuevo.
        const QList<PlanReport> first = f.revisionPublish.cyclesFor(done.issueId, 1);
        QCOMPARE(first.size(), 1);
        QCOMPARE(first.first().plan.id, done.planRunId);
        const QList<PlanReport> second = f.revisionPublish.cyclesFor(done.issueId, 2);
        QCOMPARE(second.size(), 1);
        QCOMPARE(second.first().plan.id, secondCycle);

        // Y la ronda 1 dice lo que le falta.
        const QList<Destination> pending = f.revisionPublish.pendingFor(done.issueId, 1);
        QVERIFY(pending.contains(Destination::Tracker));
        QVERIFY(pending.contains(Destination::Requirement));

        RevisionPublishService::Options options;
        options.revision = 1;
        options.outcome = QaOutcome::Observado;
        options.comment = QStringLiteral("Control de calidad GREQ 2026997 — revisión 1: Observado");
        options.documentPath = QStringLiteral("/tmp/acta-rev1.docx");
        RevisionPublishService::Result result;
        f.revisionPublish.publish(done.issueId, options, {}, [&result](const RevisionPublishService::Result& r) { result = r; });
        QVERIFY2(result.ok, qPrintable(result.steps.isEmpty() ? QString() : result.steps.first().message));

        issue = f.issues.find(done.issueId);
        QCOMPARE(issue->revisions.size(), 2);
        // Lo publicado es de la ronda 1: su comentario, su registro y su ciclo.
        QVERIFY(issue->revisions.first().jira.publishedAt.isValid());
        QVERIFY(issue->revisions.first().gesreq.registeredAt.isValid());
        QVERIFY(issue->revisions.last().jira.isEmpty());
        QVERIFY(issue->revisions.last().gesreq.isEmpty());
        QCOMPARE(f.zephyr->published.size(), 1);
        QCOMPARE(f.zephyr->published.first().cases.first().caseId, QStringLiteral("TC-101"));
        QVERIFY(f.history.findPlan(done.planRunId)->isPublished());
        QVERIFY(!f.history.findPlan(secondCycle)->isPublished());
        // Y la ronda en curso sigue abierta: publicar lo de antes no la cierra.
        QVERIFY(issue->currentRevision() != nullptr);
        QCOMPARE(issue->currentRevision()->number, 2);
    }

    // Registrada ya una ronda posterior, la anterior no se registra: lo que sabe GESREQ es lo último.
    void aLaterRegistrationBlocksTheEarlierRound() {
        AppFixture f;
        singlePhase(f);
        const Finished done = finishedRevision(f);
        anotherRound(f, done);
        f.issues.closeRevision(done.issueId, QaOutcome::Conforme);

        RevisionPublishService::Options options;
        options.zephyr = false;
        options.tracker = false;
        options.close = false;
        options.outcome = QaOutcome::Conforme;
        f.revisionPublish.publish(done.issueId, options, {}, {});
        QCOMPARE(f.requirementSource->registrations.size(), 1);

        const auto step = stepOf(f.revisionPublish.stepsFor(done.issueId, 1), Destination::Requirement);
        QVERIFY(!step.available);
        QVERIFY2(step.blocked.contains(QStringLiteral("revisión 2")), qPrintable(step.blocked));
        QVERIFY(!f.revisionPublish.pendingFor(done.issueId, 1).contains(Destination::Requirement));

        options.revision = 1;
        options.outcome = QaOutcome::Observado;
        RevisionPublishService::Result result;
        f.revisionPublish.publish(done.issueId, options, {}, [&result](const RevisionPublishService::Result& r) { result = r; });
        QVERIFY(!result.ok);
        QCOMPARE(f.requirementSource->registrations.size(), 1);
    }

    // Publicado el resultado, del issue del gestor cuelgan sus bugs y los Tests de sus ejecuciones, y
    // cada bug queda además en el paso de Zephyr del que salió.
    void publishingLinksTheBugsAndTheTestsToTheIssue() {
        AppFixture f;
        const Finished done = finishedRevision(f);
        IssueLink bug;
        bug.key = QStringLiteral("SHOP-99");
        bug.url = QStringLiteral("https://acme.atlassian.net/browse/SHOP-99");
        bug.title = QStringLiteral("El total no cambia con el cupón");
        bug.caseId = QStringLiteral("TC-101");
        bug.step = 2;
        bug.classification = QStringLiteral("A");
        // Se reportó mientras la ronda estaba abierta (sin ejecución anotada, cuenta por fechas): justo al
        // cerrarla. «Ahora» caería a veces un milisegundo después del cierre y el bug quedaría fuera.
        bug.createdAt = f.issues.find(done.issueId)->revisions.last().closedAt;
        f.bugLedger.recordIssue(bug);
        // Un bug de otro caso, que no es de estas pruebas.
        IssueLink other = bug;
        other.key = QStringLiteral("SHOP-100");
        other.caseId = QStringLiteral("TC-999");
        other.step = 0;
        f.bugLedger.recordIssue(other);

        // Zephyr le crea su Test a la ejecución, como hace con las que nunca se publicaron.
        f.zephyr->resultToReturn.createdTests.insert(QStringLiteral("TC-101"), QStringLiteral("SHOP-77"));

        RevisionPublishService::Options options;
        options.outcome = QaOutcome::Observado;
        options.comment = QStringLiteral("Observado");
        RevisionPublishService::Result result;
        f.revisionPublish.publish(done.issueId, options, {}, [&result](const RevisionPublishService::Result& r) { result = r; });
        QVERIFY(result.ok);

        // A Zephyr, el defecto va con la ejecución de su caso y con el paso en el que se vio.
        QCOMPARE(f.zephyr->published.size(), 1);
        const PublishCase& published = f.zephyr->published.first().cases.first();
        QCOMPARE(published.caseId, QStringLiteral("TC-101"));
        QCOMPARE(published.defects.size(), 1);
        QCOMPARE(published.defects.first().key, QStringLiteral("SHOP-99"));
        QCOMPARE(published.defects.first().step, 2);

        // Y al issue del gestor se enlazan el bug y el Test que Zephyr creó para la ejecución.
        const QString testKey = f.history.runs().first().testKey;
        QCOMPARE(testKey, QStringLiteral("SHOP-77"));
        QList<QPair<QString, QString>> expected{{QStringLiteral("SHOP-99"), QStringLiteral("SHOP-12")},
                                                {testKey, QStringLiteral("SHOP-12")}};
        QCOMPARE(f.tracker->links, expected);
    }

    void whatIsNotChosenIsNotSent() {
        AppFixture f;
        singlePhase(f);
        const Finished done = finishedRevision(f);
        RevisionPublishService::Options options;
        options.zephyr = false;
        options.tracker = false;
        options.close = false;
        options.outcome = QaOutcome::Conforme;
        options.comment = QStringLiteral("Conforme");

        RevisionPublishService::Result result;
        f.revisionPublish.publish(done.issueId, options, {}, [&result](const RevisionPublishService::Result& r) { result = r; });
        QVERIFY(result.ok);
        QCOMPARE(result.steps.size(), 1);
        QVERIFY(f.zephyr->published.isEmpty());
        QVERIFY(f.tracker->comments.isEmpty());
        QCOMPARE(f.requirementSource->registrations.size(), 1);
    }

    // Un paso que falla no impide los demás, y lo que se cortó sin respuesta queda sin confirmar.
    void aStepThatFailsDoesNotStopTheRest() {
        AppFixture f;
        const Finished done = finishedRevision(f);
        f.tracker->mode = testing::FakeIssueTracker::Mode::NetworkDown;
        f.requirementSource->registrationCutOff = true;

        RevisionPublishService::Options options;
        options.outcome = QaOutcome::Observado;
        options.comment = QStringLiteral("Observado");

        RevisionPublishService::Result result;
        f.revisionPublish.publish(done.issueId, options, {}, [&result](const RevisionPublishService::Result& r) { result = r; });
        QVERIFY(!result.ok);
        QCOMPARE(result.steps.size(), 3);
        QVERIFY(result.steps[0].ok);          // Zephyr sí
        QVERIFY(!result.steps[1].ok);         // el gestor no
        QVERIFY(!result.steps[2].ok);
        QVERIFY(result.steps[2].uncertain);   // GESREQ se cortó: hay que comprobarlo
        const IssueRevision& revision = f.issues.find(done.issueId)->revisions.last();
        QVERIFY(revision.gesreq.uncertain);
        QVERIFY(!revision.gesreq.registeredAt.isValid());
    }

    // Sin Zephyr activado, sin issue en el gestor y sin requerimiento importado, cada paso dice por qué.
    void withoutItsDestinationsConfiguredEveryStepSaysWhy() {
        AppFixture f;
        const QString id = f.issues.createIssue(QStringLiteral("A mano"));
        const QList<RevisionPublishService::Step> steps = f.revisionPublish.stepsFor(id);
        for (const auto destination : {Destination::Zephyr, Destination::Tracker, Destination::Requirement}) {
            const auto step = stepOf(steps, destination);
            QVERIFY(!step.available);
            QVERIFY(!step.blocked.isEmpty());
        }
        QVERIFY(stepOf(steps, Destination::Requirement).blocked.contains(QStringLiteral("importado")));
    }
};

QTEST_MAIN(RevisionPublishServiceTest)
#include "test_revision_publish_service.moc"
