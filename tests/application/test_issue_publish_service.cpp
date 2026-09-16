// IssuePublishService (application/IssuePublishService.h): borrador del issue para el gestor, creación
// explícita, vinculación de uno que ya existe, actualización de lo que cambió en QAflow, estado, el
// resultado de la revisión con su acta y envíos que se cortan sin respuesta.

#include "support/FakeIssueTracker.h"
#include "support/MemoryRepositories.h"

#include "application/IssuePublishService.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::FakeIssueTracker;
using qaflow::testing::MemoryIssueRepository;
using qaflow::testing::MemorySettingsRepository;

namespace {
const QString kConnection = QStringLiteral("http://gesreq.test:7401/greq");

ExternalRequirement requirement() {
    ExternalRequirement r;
    r.id = QStringLiteral("2025175");
    r.system = QStringLiteral("SUMA TRANSITO-TRANSITOS");
    r.systemCode = QStringLiteral("SUMA TRANSITO");
    r.summary = QStringLiteral("Desarrollo complementario del laboratorio");
    r.priority = QStringLiteral("ALTA");
    r.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
    r.assignedFrom = QDate(2025, 8, 11);
    r.assignedUntil = QDate(2026, 1, 27);
    r.requester = QStringLiteral("PÉREZ GÓMEZ ANA");
    r.detailUrl = kConnection + QStringLiteral("/publico.do?id=2025175&bandera=1");
    return r;
}

/// Un issue importado de GESREQ con sus notas, el gestor falso configurado y el servicio listo.
struct Fixture {
    std::shared_ptr<FakeIssueTracker> tracker = std::make_shared<FakeIssueTracker>();
    std::shared_ptr<MemoryIssueRepository> repo = std::make_shared<MemoryIssueRepository>();
    std::shared_ptr<MemorySettingsRepository> settingsRepo = std::make_shared<MemorySettingsRepository>();
    IssueStore issues{repo};
    SettingsStore settings{settingsRepo};
    IssuePublishService service{tracker, issues, settings};
    QString id;

    Fixture() {
        issues.load();
        settings.load();
        settings.updateTracker([](TrackerSettings& t) {
            t.kind = TrackerKind::Jira;
            t.url = QStringLiteral("https://jira.example.test");
            t.project = QStringLiteral("SHOP");
            t.user = QStringLiteral("aperez");
            t.token = QStringLiteral("s3creta");
            t.connected = true;
        });
        id = issues.importRequirements({requirement()}, kConnection).created.first();
        issues.updateIssue(id, [](Issue& i) { i.notes = QStringLiteral("Probar con el usuario de calidad"); });
    }
    const Issue& issue() const { return *issues.find(id); }
};
} // namespace

class IssuePublishServiceTest : public QObject {
    Q_OBJECT
private slots:
    void aManualIssueKeepsItsTitle() {
        Fixture f;
        Issue issue;
        issue.title = QStringLiteral("Pruebas manuales");
        QCOMPARE(f.service.draftFor(issue).summary, QStringLiteral("Pruebas manuales"));
    }

    void theDraftCarriesTheRequirementAndTheQaNotes() {
        Fixture f;
        QVERIFY(f.service.canPublish());
        QCOMPARE(f.service.destination(), QStringLiteral("Jira · SHOP"));
        const IssueDraft draft = f.service.draftFor(f.issue());
        QCOMPARE(draft.summary, QStringLiteral("QA - 2025175 - Desarrollo complementario del laboratorio"));
        QVERIFY2(draft.description.contains(QStringLiteral("GESREQ 2025175")), qPrintable(draft.description));
        QVERIFY(draft.description.contains(QStringLiteral("SUMA TRANSITO")));
        QVERIFY(draft.description.contains(QStringLiteral("CONTROL CALIDAD ASIGNADO")));
        QVERIFY(draft.description.contains(QStringLiteral("11/08/2025 – 27/01/2026")));
        QVERIFY(draft.description.contains(QStringLiteral("Probar con el usuario de calidad")));
        QVERIFY2(draft.description.contains(f.id), qPrintable(draft.description));
        // Etiquetas con las que encontrarlo en el gestor (también si un envío queda sin confirmar).
        QVERIFY(draft.labels.contains(QStringLiteral("qaflow")));
        QVERIFY(draft.labels.contains(f.id));
        QVERIFY(draft.labels.contains(QStringLiteral("GREQ-2025175")));
        QCOMPARE(draft.issueType, QStringLiteral("Tarea"));
    }

    void publishingStoresWhereItWentAndWhatSeSent() {
        Fixture f;
        IssuePublishService::Result out;
        f.service.publish(f.id, f.service.draftFor(f.issue()), [&](const IssuePublishService::Result& r) { out = r; });
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(f.tracker->publishedIssues.size(), 1);
        QCOMPARE(f.tracker->publishedIssues.first().summary,
                 QStringLiteral("QA - 2025175 - Desarrollo complementario del laboratorio"));
        QCOMPARE(f.tracker->publishedIssues.first().issueType, QStringLiteral("Tarea"));

        const IssuePublication& p = f.issue().publication;
        QVERIFY(f.issue().isPublished());
        QCOMPARE(p.key, out.key);
        QCOMPARE(p.url, QStringLiteral("https://jira.example.test/browse/%1").arg(out.key));
        QCOMPARE(p.tracker, QStringLiteral("Jira"));
        QCOMPARE(p.project, QStringLiteral("SHOP"));
        QCOMPARE(p.baseUrl, QStringLiteral("https://jira.example.test"));
        QVERIFY(p.publishedAt.isValid());
        QVERIFY(!p.linked);
        QVERIFY(!p.uncertain);
        QCOMPARE(p.publishedTitle, QStringLiteral("QA - 2025175 - Desarrollo complementario del laboratorio"));
        QVERIFY(!f.service.needsUpdate(f.issue()));   // recién publicado: nada pendiente
    }

    // Lo que se edita después queda pendiente de actualizar, y sólo «Actualizar» reescribe el gestor.
    void whatChangesAfterPublishingIsPendingUntilItIsUpdated() {
        Fixture f;
        f.service.publish(f.id, f.service.draftFor(f.issue()), [](const IssuePublishService::Result&) {});
        const QString key = f.issue().publication.key;

        f.issues.updateIssue(f.id, [](Issue& i) { i.title = QStringLiteral("Pruebas del laboratorio"); });
        QVERIFY(f.service.needsUpdate(f.issue()));
        QVERIFY(f.tracker->updatedIssues.isEmpty());   // nada se sobrescribe solo

        IssuePublishService::Result out;
        f.service.update(f.id, f.service.draftFor(f.issue()), [&](const IssuePublishService::Result& r) { out = r; });
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(f.tracker->updatedKeys, QStringList{key});
        QCOMPARE(f.tracker->updatedIssues.first().summary, QStringLiteral("QA - 2025175 - Pruebas del laboratorio"));
        QCOMPARE(f.issue().publication.publishedTitle, QStringLiteral("QA - 2025175 - Pruebas del laboratorio"));
        QVERIFY(!f.service.needsUpdate(f.issue()));
        QCOMPARE(f.issue().publication.key, key);   // sigue siendo el mismo issue del gestor
    }

    // Sin respuesta no se sabe si el gestor llegó a crearlo: queda marcado para comprobarlo antes de reintentar.
    void aSendThatIsCutOffIsLeftUnconfirmed() {
        Fixture f;
        f.tracker->mode = FakeIssueTracker::Mode::NetworkDown;
        IssuePublishService::Result out;
        f.service.publish(f.id, f.service.draftFor(f.issue()), [&](const IssuePublishService::Result& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(out.uncertain);
        QVERIFY(out.retryable);
        QVERIFY(!f.issue().isPublished());
        QVERIFY(f.issue().publication.uncertain);
        QCOMPARE(f.issue().publication.lastError, QStringLiteral("Host not found"));

        f.tracker->mode = FakeIssueTracker::Mode::Succeed;
        f.service.publish(f.id, f.service.draftFor(f.issue()), [&](const IssuePublishService::Result& r) { out = r; });
        QVERIFY2(out.ok, qPrintable(out.error));
        QVERIFY(!f.issue().publication.uncertain);
        QVERIFY(f.issue().publication.lastError.isEmpty());
    }

    // Un rechazo del contenido no deja dudas: no se creó nada, así que no queda «sin confirmar».
    void aRejectedContentIsNotLeftUnconfirmed() {
        Fixture f;
        f.tracker->mode = FakeIssueTracker::Mode::RejectContent;
        IssuePublishService::Result out;
        f.service.publish(f.id, f.service.draftFor(f.issue()), [&](const IssuePublishService::Result& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(!out.uncertain);
        QVERIFY(!f.issue().publication.uncertain);
        QVERIFY2(out.error.contains(QStringLiteral("issuetype")), qPrintable(out.error));
    }

    void linkingAnIssueThatAlreadyExistsDoesNotCreateAnother() {
        Fixture f;
        f.tracker->issueToReturn.title = QStringLiteral("Laboratorio · QA");
        f.tracker->issueToReturn.issueType = QStringLiteral("Historia");
        f.tracker->issueToReturn.status = QStringLiteral("In Progress");
        IssuePublishService::Result out;
        f.service.link(f.id, QStringLiteral(" QA-9 "), [&](const IssuePublishService::Result& r) { out = r; });
        QVERIFY2(out.ok, qPrintable(out.error));
        QVERIFY(f.tracker->publishedIssues.isEmpty());

        const IssuePublication& p = f.issue().publication;
        QCOMPARE(p.key, QStringLiteral("QA-9"));
        QCOMPARE(p.url, QStringLiteral("https://jira.example.test/browse/QA-9"));
        QCOMPARE(p.issueType, QStringLiteral("Historia"));
        QCOMPARE(p.status, QStringLiteral("In Progress"));
        QVERIFY(p.linked);
        // Lo vinculado lo escribió otro: QAflow no ofrece sobrescribirlo.
        QVERIFY(!f.service.needsUpdate(f.issue()));

        f.service.unlink(f.id);
        QVERIFY(!f.issue().isPublished());
        QVERIFY(f.tracker->updatedIssues.isEmpty());   // desvincular no toca el gestor
    }

    void linkingAKeyThatIsNotThereSaysSo() {
        Fixture f;
        f.tracker->issueExists = false;
        IssuePublishService::Result out;
        f.service.link(f.id, QStringLiteral("QA-404"), [&](const IssuePublishService::Result& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY2(out.error.contains(QStringLiteral("QA-404")), qPrintable(out.error));
        QVERIFY(!f.issue().isPublished());

        f.service.link(f.id, QStringLiteral("  "), [&](const IssuePublishService::Result& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(!f.issue().isPublished());
    }

    void theStatusOfThePublishedIssueIsRead() {
        Fixture f;
        f.service.publish(f.id, f.service.draftFor(f.issue()), [](const IssuePublishService::Result&) {});
        f.tracker->statusToReturn = QStringLiteral("Done");
        f.tracker->resolvedToReturn = true;
        IssuePublishService::Result out;
        f.service.refreshStatus(f.id, [&](const IssuePublishService::Result& r) { out = r; });
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(f.issue().publication.status, QStringLiteral("Done"));
        QVERIFY(f.issue().publication.resolved);
        QVERIFY(f.issue().publication.statusCheckedAt.isValid());
        QCOMPARE(f.tracker->statusQueries.size(), 1);
    }

    void theIssueTypesComeFromTheProjectAndAreAskedOnce() {
        Fixture f;
        f.tracker->metadataToReturn.issueTypes = {QStringLiteral("Error"), QStringLiteral("Tarea")};
        QStringList types;
        f.service.fetchIssueTypes([&](const QStringList& t) { types = t; });
        QCOMPARE(types, (QStringList{QStringLiteral("Error"), QStringLiteral("Tarea")}));
        f.service.fetchIssueTypes([&](const QStringList& t) { types = t; });
        QCOMPARE(f.tracker->metadataCalls, 1);
        QCOMPARE(IssuePublishService::defaultIssueType(types), QStringLiteral("Tarea"));
        QCOMPARE(IssuePublishService::defaultIssueType({QStringLiteral("Bug"), QStringLiteral("Story")}), QStringLiteral("Story"));
        QCOMPARE(IssuePublishService::defaultIssueType({}), QStringLiteral("Tarea"));
    }

    // ---- El resultado de la revisión --------------------------------------------------------------
    void theRevisionResultIsCommentedInTheTrackerWithItsRecord() {
        Fixture f;
        QVERIFY(!f.service.canPublishResult(f.issue()));   // sin publicar no hay dónde comentar
        f.service.publish(f.id, f.service.draftFor(f.issue()), [](const IssuePublishService::Result&) {});
        QVERIFY(f.service.canPublishResult(f.issue()));
        f.issues.openRevision(f.id);

        IssuePublishService::Result result;
        f.service.publishResult(f.id, QStringLiteral("GREQ 2025175 — revisión 1: Observado"), QStringLiteral("/tmp/acta.docx"),
                                [&result](const IssuePublishService::Result& r) { result = r; });
        QVERIFY(result.ok);
        QCOMPARE(f.tracker->commentedKeys.size(), 1);
        QCOMPARE(f.tracker->commentedKeys.first(), f.issue().publication.key);
        QVERIFY(f.tracker->comments.first().contains(QStringLiteral("revisión 1: Observado")));
        QCOMPARE(f.tracker->commentAttachments.first(), QStringList{QStringLiteral("/tmp/acta.docx")});

        const IssueRevision* revision = f.issue().currentRevision();
        QVERIFY(revision);
        QCOMPARE(revision->jira.key, f.issue().publication.key);
        QVERIFY(revision->jira.publishedAt.isValid());
        QVERIFY(revision->jira.attachedDocument);
        QVERIFY(!revision->jira.uncertain);
    }

    void aResultThatIsCutOffIsLeftUnconfirmed() {
        Fixture f;
        f.service.publish(f.id, f.service.draftFor(f.issue()), [](const IssuePublishService::Result&) {});
        f.issues.openRevision(f.id);
        f.tracker->mode = FakeIssueTracker::Mode::NetworkDown;

        IssuePublishService::Result result;
        f.service.publishResult(f.id, QStringLiteral("resumen"), QString(),
                                [&result](const IssuePublishService::Result& r) { result = r; });
        QVERIFY(!result.ok);
        QVERIFY(result.uncertain);
        const IssueRevision* revision = f.issue().currentRevision();
        QVERIFY(revision->jira.uncertain);
        QCOMPARE(revision->jira.lastError, QStringLiteral("Host not found"));
        QVERIFY(!revision->jira.publishedAt.isValid());

        // Un rechazo del contenido no deja esa duda.
        f.tracker->mode = FakeIssueTracker::Mode::RejectContent;
        f.service.publishResult(f.id, QStringLiteral("resumen"), QString(),
                                [&result](const IssuePublishService::Result& r) { result = r; });
        QVERIFY(!result.ok);
        QVERIFY(!result.uncertain);
        QVERIFY(!f.issue().currentRevision()->jira.uncertain);
    }

    void withoutProjectOrTrackerItDoesNotPublish() {
        Fixture f;
        f.settings.updateTracker([](TrackerSettings& t) { t.project.clear(); });
        QVERIFY(!f.service.canPublish());
        QVERIFY(f.service.destination().isEmpty());
        IssuePublishService::Result out;
        f.service.publish(f.id, f.service.draftFor(f.issue()), [&](const IssuePublishService::Result& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(!out.error.isEmpty());
        QVERIFY(f.tracker->publishedIssues.isEmpty());

        f.settings.updateTracker([](TrackerSettings& t) { t.project = QStringLiteral("SHOP"); t.kind = TrackerKind::GitHub; });
        f.tracker->publishesIssues = false;   // como GitHub, GitLab o Azure, que todavía no publican issues
        QVERIFY(!f.service.canPublish());
    }
};

QTEST_MAIN(IssuePublishServiceTest)
#include "test_issue_publish_service.moc"
