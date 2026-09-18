// BugReportService (application/BugReportService.h): borrador, envío, cola offline,
// estados y metadatos, sobre un gestor falso.

#include "support/AppFixture.h"
#include "support/FakeIssueTracker.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;
using qaflow::testing::FakeIssueTracker;

class BugReportServiceTest : public QObject {
    Q_OBJECT
private slots:
    void draftUsesFailedStepAndCaseMetadata() {
        AppFixture f;
        f.store.select(QStringLiteral("TC-104"));   // componente Carrito, historia SHOP-12
        f.run.start(QStringLiteral("TC-104"));
        f.run.mark(StepResult::Pass);
        f.run.setNote(QStringLiteral("no descuenta"));
        f.run.mark(StepResult::Fail);

        const BugReport d = f.bugs.draftFromCurrentContext();
        QVERIFY(d.title.contains(QStringLiteral("Falla en paso 2")));
        QCOMPARE(d.actual, QStringLiteral("no descuenta"));
        QCOMPARE(d.linkedCaseId, QStringLiteral("TC-104"));
        QCOMPARE(d.linkedStoryKey, QStringLiteral("SHOP-12"));
        QCOMPARE(d.components, QStringList{QStringLiteral("Carrito")});
        QCOMPARE(d.linkedRunId, f.run.state().runId);   // de qué pruebas sale el bug
        QCOMPARE(d.linkedPlanRunId, f.run.planRunId());
        QCOMPARE(d.priority, QStringLiteral("Medium"));   // severidad Mayor por defecto
        QVERIFY(d.stepsToReproduce.startsWith(QStringLiteral("1. ")));
    }

    /// Un paso bloqueado da un parte bloqueante, y quien lo abre puede pedir otro paso distinto
    /// del que la ejecución elegiría sola.
    void draftFollowsTheBlockedStepAndTheRequestedOne() {
        AppFixture f;
        f.store.select(QStringLiteral("TC-104"));   // 4 pasos
        f.run.start(QStringLiteral("TC-104"));
        f.run.mark(StepResult::Pass);
        f.run.setNote(QStringLiteral("el servicio no responde"));
        f.run.mark(StepResult::Block);              // la ejecución sigue abierta en el paso 3

        const BugReport blocked = f.bugs.draftFromCurrentContext();
        QVERIFY(blocked.title.contains(QStringLiteral("Bloqueo en paso 2")));
        QCOMPARE(blocked.linkedStep, 2);
        QCOMPARE(blocked.severity, QStringLiteral("Bloqueante"));
        QCOMPARE(blocked.priority, QStringLiteral("Highest"));
        QCOMPARE(blocked.actual, QStringLiteral("el servicio no responde"));

        // Reportar el paso que se tiene delante, aunque todavía no tenga veredicto.
        const BugReport current = f.bugs.draftFromCurrentContext(2);
        QVERIFY(current.title.contains(QStringLiteral("Falla en paso 3")));
        QCOMPARE(current.linkedStep, 3);
        QCOMPARE(current.severity, QStringLiteral("Mayor"));
    }

    void successfulSubmitRecordsLinkedIssue() {
        AppFixture f;
        // Los bugs se encuentran ejecutando: el parte se abre desde la ejecución en curso.
        f.run.startSequence({QStringLiteral("TC-104")}, QStringLiteral("Regresión"));
        f.run.mark(StepResult::Fail);
        BugReport b = f.bugs.draftFromCurrentContext();
        b.title = QStringLiteral("Falla el cupón"); b.actual = QStringLiteral("no descuenta");
        BugReportService::SubmitResult out;
        f.bugs.submit(b, [&](const BugReportService::SubmitResult& r) { out = r; });
        QVERIFY(out.ok);
        QCOMPARE(out.key, QStringLiteral("SHOP-100"));
        QCOMPARE(out.url, QStringLiteral("https://acme.atlassian.net/browse/SHOP-100"));
        QCOMPARE(f.bugLedger.issues().size(), 1);
        const IssueLink& l = f.bugLedger.issues().first();
        QCOMPARE(l.caseId, QStringLiteral("TC-104"));
        QCOMPARE(l.tracker, QStringLiteral("Jira"));
        // El bug queda colgado de las pruebas de las que salió: la ejecución en curso y su ciclo.
        QCOMPARE(l.runId, f.run.state().runId);
        QVERIFY(!l.runId.isEmpty());
        QCOMPARE(l.planRunId, f.run.planRunId());
        QCOMPARE(l.title, QStringLiteral("Falla el cupón"));
        QCOMPARE(f.bugLedger.issuesForCase(QStringLiteral("TC-104")).size(), 1);
        QVERIFY(f.bugLedger.pending().isEmpty());
    }

    void networkFailureQueuesTheBug() {
        AppFixture f;
        f.tracker->mode = FakeIssueTracker::Mode::NetworkDown;
        BugReport b;
        b.title = QStringLiteral("t"); b.actual = QStringLiteral("a"); b.linkedCaseId = QStringLiteral("TC-101");
        BugReportService::SubmitResult out;
        f.bugs.submit(b, [&](const BugReportService::SubmitResult& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(out.queued);
        QCOMPARE(f.bugLedger.pending().size(), 1);
        QCOMPARE(f.bugLedger.pending().first().lastError, QStringLiteral("Host not found"));
        QVERIFY(f.bugLedger.issues().isEmpty());
    }

    void contentRejectionIsNotQueued() {
        AppFixture f;
        f.tracker->mode = FakeIssueTracker::Mode::RejectContent;
        BugReport b;
        b.title = QStringLiteral("t"); b.actual = QStringLiteral("a");
        BugReportService::SubmitResult out;
        f.bugs.submit(b, [&](const BugReportService::SubmitResult& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(!out.queued);
        QVERIFY(out.error.contains(QStringLiteral("HTTP 400")));
        QVERIFY(f.bugLedger.pending().isEmpty());
    }

    void retryPendingSendsInOrderAndStopsWhenOffline() {
        AppFixture f;
        f.tracker->mode = FakeIssueTracker::Mode::NetworkDown;
        for (const auto& t : {"uno", "dos", "tres"}) {
            BugReport b;
            b.title = QLatin1String(t); b.actual = QStringLiteral("a");
            f.bugs.submit(b, [](const BugReportService::SubmitResult&) {});
        }
        QCOMPARE(f.bugLedger.pending().size(), 3);

        // Sigue sin red: nada se envía y se detiene al primer fallo.
        BugReportService::RetryResult r;
        f.bugs.retryPending([&](const BugReportService::RetryResult& x) { r = x; });
        QCOMPARE(r.sent, 0);
        QCOMPARE(r.failed, 3);
        QCOMPARE(f.bugLedger.pending().first().attempts, 2);

        // Vuelve la red: se envían en orden y la cola se vacía.
        f.tracker->mode = FakeIssueTracker::Mode::Succeed;
        f.bugs.retryPending([&](const BugReportService::RetryResult& x) { r = x; });
        QCOMPARE(r.sent, 3);
        QCOMPARE(r.failed, 0);
        QCOMPARE(r.keys, (QStringList{QStringLiteral("SHOP-100"), QStringLiteral("SHOP-101"), QStringLiteral("SHOP-102")}));
        QVERIFY(f.bugLedger.pending().isEmpty());
        QCOMPARE(f.bugLedger.issues().size(), 3);
        QCOMPARE(f.bugLedger.issues()[0].title, QStringLiteral("uno"));
    }

    void retrySkipsRejectedContentAndContinues() {
        AppFixture f;
        f.tracker->mode = FakeIssueTracker::Mode::NetworkDown;
        BugReport b;
        b.title = QStringLiteral("t"); b.actual = QStringLiteral("a");
        f.bugs.submit(b, [](const BugReportService::SubmitResult&) {});
        f.bugs.submit(b, [](const BugReportService::SubmitResult&) {});
        f.tracker->mode = FakeIssueTracker::Mode::RejectContent;
        BugReportService::RetryResult r;
        f.bugs.retryPending([&](const BugReportService::RetryResult& x) { r = x; });
        QCOMPARE(r.sent, 0);
        QCOMPARE(r.failed, 2);                    // los dos se intentaron
        QCOMPARE(f.bugLedger.pending().size(), 2); // y siguen en la cola con su error
        QVERIFY(f.bugLedger.pending().first().lastError.contains(QStringLiteral("HTTP 400")));
        f.bugs.discardPending(f.bugLedger.pending().first().id);
        QCOMPARE(f.bugLedger.pending().size(), 1);
    }

    void refreshStatusesQueriesOnlyCurrentTrackerIssues() {
        AppFixture f;
        BugReport b;
        b.title = QStringLiteral("t"); b.actual = QStringLiteral("a");
        f.bugs.submit(b, [](const BugReportService::SubmitResult&) {});
        IssueLink other;
        other.key = QStringLiteral("#3"); other.tracker = QStringLiteral("GitHub");
        f.bugLedger.recordIssue(other);

        f.tracker->statusToReturn = QStringLiteral("Done");
        BugReportService::RefreshResult r;
        f.bugs.refreshStatuses(false, [&](const BugReportService::RefreshResult& x) { r = x; });
        QCOMPARE(r.updated, 1);
        QCOMPARE(f.tracker->statusQueries, QStringList{QStringLiteral("SHOP-100")});
        QVERIFY(f.bugLedger.findIssue(QStringLiteral("SHOP-100"))->resolved);
        QCOMPARE(f.bugLedger.openIssueCount(), 1);   // el de GitHub sigue "abierto"

        f.bugs.refreshStatuses(true, [&](const BugReportService::RefreshResult& x) { r = x; });   // sólo abiertos: ninguno de Jira
        QCOMPARE(r.updated, 0);
    }

    /// La pantalla de bugs trae del gestor los que creó QAflow: los que ya están se actualizan con
    /// lo que diga hoy Jira y los que no (reportados desde otro equipo) entran en el libro, con su
    /// caso sacado de las etiquetas.
    void importBringsTheTrackerBugsIntoTheLedger() {
        AppFixture f;
        BugReport b;
        b.title = QStringLiteral("El cupón no descuenta"); b.actual = QStringLiteral("a");
        b.linkedCaseId = QStringLiteral("TC-104"); b.linkedStep = 2; b.severity = QStringLiteral("Mayor");
        f.bugs.submit(b, [](const BugReportService::SubmitResult&) {});   // SHOP-100, sin estado

        TrackerIssueInfo known;      // el mismo bug, ya cerrado en Jira y con el título retocado
        known.key = QStringLiteral("SHOP-100");
        known.title = QStringLiteral("[Checkout] El cupón no descuenta");
        known.issueType = QStringLiteral("Error");
        known.status = QStringLiteral("Done");
        known.resolved = true;
        TrackerIssueInfo foreign;    // uno que reportó otro equipo desde su QAflow (trae su etiqueta)
        foreign.key = QStringLiteral("SHOP-155");
        foreign.title = QStringLiteral("Error 500 al pagar");
        foreign.status = QStringLiteral("In Progress");
        foreign.createdAt = QDateTime(QDate(2026, 9, 12), QTime(9, 30));
        foreign.issueType = QStringLiteral("Mejora");
        foreign.labels = {QStringLiteral("qaflow"), QStringLiteral("TC-101"), QStringLiteral("regresión")};
        f.tracker->issuesToReturn = {known, foreign};

        BugReportService::ImportResult r;
        QVERIFY(f.bugs.canImportFromTracker());
        f.bugs.importFromTracker(0, [&](const BugReportService::ImportResult& x) { r = x; });
        QVERIFY(r.ok);
        QCOMPARE(r.updated, 1);
        QCOMPARE(r.imported, 1);
        QCOMPARE(r.total, 2);
        QVERIFY(!r.hasMore());          // caben en una página
        QCOMPARE(f.bugLedger.issues().size(), 2);

        const IssueLink* mine = f.bugLedger.findIssue(QStringLiteral("SHOP-100"));
        QCOMPARE(mine->status, QStringLiteral("Done"));
        QVERIFY(mine->resolved);
        QCOMPARE(mine->title, QStringLiteral("[Checkout] El cupón no descuenta"));
        QCOMPARE(mine->caseId, QStringLiteral("TC-104"));   // lo que es de QAflow no se toca
        QCOMPARE(mine->step, 2);
        QCOMPARE(mine->severity, QStringLiteral("Mayor"));
        QCOMPARE(mine->issueType, QStringLiteral("Error"));

        const IssueLink* his = f.bugLedger.findIssue(QStringLiteral("SHOP-155"));
        QCOMPARE(his->caseId, QStringLiteral("TC-101"));    // la etiqueta que es un caso del proyecto
        QCOMPARE(his->createdAt.date(), QDate(2026, 9, 12));
        QCOMPARE(his->issueType, QStringLiteral("Mejora"));   // en la lista se ve que es una mejora
        QVERIFY(!his->resolved);
        QCOMPARE(f.bugLedger.openIssueCount(), 1);   // el mío quedó cerrado; el ajeno sigue abierto
    }

    /// Un bug de QAflow sin etiqueta de caso entra igual: sin caso ni paso, que es lo que se sabe de él.
    void importBringsBugsWithoutACaseLabel() {
        AppFixture f;
        TrackerIssueInfo other;
        other.key = QStringLiteral("SHOP-200");
        other.title = QStringLiteral("La búsqueda tarda 8 s");
        other.status = QStringLiteral("To Do");
        other.labels = {QStringLiteral("qaflow")};
        f.tracker->issuesToReturn = {other};
        BugReportService::ImportResult r;
        f.bugs.importFromTracker(0, [&](const BugReportService::ImportResult& x) { r = x; });
        QVERIFY(r.ok);
        QCOMPARE(r.imported, 1);
        const IssueLink* l = f.bugLedger.findIssue(QStringLiteral("SHOP-200"));
        QVERIFY(l);
        QVERIFY(l->caseId.isEmpty());
        QCOMPARE(l->step, 0);
        QCOMPARE(l->tracker, QStringLiteral("Jira"));
    }

    /// El gestor se recorre por páginas: cada llamada trae las suyas y dice desde dónde seguir.
    void importWalksTheTrackerOnePageAtATime() {
        AppFixture f;
        QList<TrackerIssueInfo> many;
        for (int i = 0; i < BugReportService::kImportPage + 3; ++i) {
            TrackerIssueInfo info;
            info.key = QStringLiteral("SHOP-%1").arg(300 + i);
            info.title = QStringLiteral("bug %1").arg(i);
            many << info;
        }
        f.tracker->issuesToReturn = many;

        BugReportService::ImportResult first;
        f.bugs.importFromTracker(0, [&](const BugReportService::ImportResult& x) { first = x; });
        QVERIFY(first.ok);
        QCOMPARE(first.imported, BugReportService::kImportPage);
        QCOMPARE(first.total, many.size());
        QCOMPARE(first.nextStart, BugReportService::kImportPage);
        QCOMPARE(f.bugLedger.issues().size(), BugReportService::kImportPage);

        BugReportService::ImportResult second;
        f.bugs.importFromTracker(first.nextStart, [&](const BugReportService::ImportResult& x) { second = x; });
        QVERIFY(second.ok);
        QCOMPARE(second.imported, 3);
        QVERIFY(!second.hasMore());
        QCOMPARE(f.bugLedger.issues().size(), many.size());
        QCOMPARE(f.tracker->issueSearchStarts, (QList<int>{0, BugReportService::kImportPage}));
    }

    /// Sin gestor que sepa buscar, el botón no se ofrece y la llamada lo dice sin tocar el libro.
    void importIsRefusedWhenTheTrackerCannotSearch() {
        AppFixture f;
        f.tracker->searchesIssues = false;
        QVERIFY(!f.bugs.canImportFromTracker());
        BugReportService::ImportResult r;
        f.bugs.importFromTracker(0, [&](const BugReportService::ImportResult& x) { r = x; });
        QVERIFY(!r.ok);
        QVERIFY(!r.error.isEmpty());
        QCOMPARE(f.tracker->issueSearchCalls, 0);   // ni se intenta
        QVERIFY(f.bugLedger.issues().isEmpty());
    }

    void metadataIsCachedPerProjectAndInvalidatedOnChange() {
        AppFixture f;
        f.tracker->metadataToReturn.issueTypes = {QStringLiteral("Bug"), QStringLiteral("Task")};
        f.tracker->metadataToReturn.assignees = {Assignee{QStringLiteral("abc"), QStringLiteral("Ana")}};
        QSignalSpy changed(&f.bugs, &BugReportService::metadataChanged);
        MetadataResult r;
        f.bugs.loadMetadata(false, [&](const MetadataResult& x) { r = x; });
        QVERIFY(r.ok);
        QVERIFY(f.bugs.hasMetadata());
        QCOMPARE(f.bugs.metadata().issueTypes.size(), 2);
        QCOMPARE(changed.count(), 1);
        f.bugs.loadMetadata(false, [&](const MetadataResult& x) { r = x; });
        QCOMPARE(f.tracker->metadataCalls, 1);    // caché
        f.bugs.loadMetadata(true, [&](const MetadataResult& x) { r = x; });
        QCOMPARE(f.tracker->metadataCalls, 2);    // forzado
        f.settings.updateTracker([](TrackerSettings& s) { s.project = QStringLiteral("OTRO"); });
        QVERIFY(!f.bugs.hasMetadata());           // otro proyecto: caché invalidada
    }

    // Jira busca personas en el servidor: lo escrito viaja tal cual y se devuelve lo que responde.
    void assigneesAreSearchedOnTheServerWhenTheTrackerCan() {
        AppFixture f;
        f.tracker->searchesAssignees = true;
        f.tracker->assigneesToReturn = {Assignee{QStringLiteral("aperez"), QStringLiteral("Ana Pérez")}};
        QVERIFY(f.bugs.searchesAssigneesOnServer());
        AssigneeSearch r;
        f.bugs.searchAssignees(QStringLiteral("ana"), [&](const AssigneeSearch& x) { r = x; });
        QVERIFY(r.ok);
        QCOMPARE(r.assignees.size(), 1);
        QCOMPARE(r.assignees[0].id, QStringLiteral("aperez"));
        QCOMPARE(f.tracker->assigneeQueries, QStringList{QStringLiteral("ana")});
    }

    // GitHub, GitLab y Azure no saben buscar: se filtra en local lo que trajo el proyecto.
    void assigneesAreFilteredLocallyWhenTheTrackerCannotSearch() {
        AppFixture f;
        f.tracker->metadataToReturn.assignees = {Assignee{QStringLiteral("aperez"), QStringLiteral("Ana Pérez")},
                                                 Assignee{QStringLiteral("lgarcia"), QStringLiteral("Luis García")}};
        f.bugs.loadMetadata(false, [](const MetadataResult&) {});
        QVERIFY(!f.bugs.searchesAssigneesOnServer());
        AssigneeSearch r;
        f.bugs.searchAssignees(QStringLiteral("luis"), [&](const AssigneeSearch& x) { r = x; });
        QVERIFY(r.ok);
        QCOMPARE(r.assignees.size(), 1);
        QCOMPARE(r.assignees[0].name, QStringLiteral("Luis García"));
        QVERIFY(f.tracker->assigneeQueries.isEmpty());   // no se llamó al gestor

        f.bugs.searchAssignees(QStringLiteral("aperez"), [&](const AssigneeSearch& x) { r = x; });   // también por id
        QCOMPARE(r.assignees.size(), 1);
        f.bugs.searchAssignees(QString(), [&](const AssigneeSearch& x) { r = x; });
        QCOMPARE(r.assignees.size(), 2);
    }

    // Si la búsqueda falla se conserva lo ya cargado y el error llega al formulario.
    void failedAssigneeSearchFallsBackToTheLoadedList() {
        AppFixture f;
        f.tracker->searchesAssignees = true;
        f.tracker->metadataToReturn.assignees = {Assignee{QStringLiteral("aperez"), QStringLiteral("Ana Pérez")}};
        f.bugs.loadMetadata(false, [](const MetadataResult&) {});
        f.tracker->mode = FakeIssueTracker::Mode::NetworkDown;
        AssigneeSearch r;
        f.bugs.searchAssignees(QStringLiteral("ana"), [&](const AssigneeSearch& x) { r = x; });
        QVERIFY(!r.ok);
        QCOMPARE(r.error, QStringLiteral("Host not found"));
        QCOMPARE(r.assignees.size(), 1);   // sigue habiendo con qué trabajar
    }
};

QTEST_APPLESS_MAIN(BugReportServiceTest)
#include "test_bug_report_service.moc"
