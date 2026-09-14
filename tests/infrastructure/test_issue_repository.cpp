// JsonIssueRepository (infrastructure/persistence/): issues.json de ida y vuelta con todo lo escrito en
// QAflow y lo importado de GESREQ; sin fichero no hay issues, y un fichero dañado no se lee como vacío.

#include "infrastructure/persistence/JsonIssueRepository.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace qaflow;

namespace {
Issue fullIssue() {
    Issue i;
    i.id = QStringLiteral("IS-0007");
    i.title = QStringLiteral("Pruebas del laboratorio");
    i.notes = QStringLiteral("Usar el usuario de calidad");
    i.priority = Priority::Alta;
    i.state = IssueState::Preparing;
    i.caseIds = {QStringLiteral("TC-101"), QStringLiteral("TC-104")};
    i.planIds = {QStringLiteral("PL-0002")};
    i.publication.tracker = QStringLiteral("Jira");
    i.publication.baseUrl = QStringLiteral("https://jira.example.test");
    i.publication.project = QStringLiteral("QA");
    i.publication.key = QStringLiteral("QA-12");
    i.publication.url = QStringLiteral("https://jira.example.test/browse/QA-12");
    i.publication.issueType = QStringLiteral("Tarea");
    i.publication.publishedAt = QDateTime(QDate(2026, 9, 2), QTime(10, 0));
    i.publication.publishedTitle = QStringLiteral("Pruebas del laboratorio");
    i.publication.publishedDescription = QStringLiteral("Requerimiento GESREQ 2025175\n* Sistema: SUMA TRANSITO");
    i.publication.status = QStringLiteral("In Progress");
    i.publication.statusCheckedAt = QDateTime(QDate(2026, 9, 3), QTime(9, 0));
    i.publication.uncertain = true;
    i.publication.lastError = QStringLiteral("Host not found");
    i.createdAt = QDateTime(QDate(2026, 9, 1), QTime(9, 30));
    i.updatedAt = QDateTime(QDate(2026, 9, 2), QTime(10, 0));

    RequirementLink& r = i.requirement;
    r.connection = QStringLiteral("http://gesreq.test:7401/greq");
    r.data.id = QStringLiteral("2025175");
    r.data.system = QStringLiteral("SUMA TRANSITO-TRANSITOS");
    r.data.systemCode = QStringLiteral("SUMA TRANSITO");
    r.data.systemName = QStringLiteral("TRANSITOS");
    r.data.summary = QStringLiteral("Desarrollo complementario");
    r.data.requestedOn = QDate(2025, 5, 15);
    r.data.assignedFrom = QDate(2025, 8, 11);
    r.data.assignedUntil = QDate(2026, 1, 27);
    r.data.requestingUnit = QStringLiteral("GNN");
    r.data.requester = QStringLiteral("PÉREZ GÓMEZ ANA");
    r.data.user = QStringLiteral("LÓPEZ RUIZ CARLOS");
    r.data.priority = QStringLiteral("ALTA");
    r.data.states = {QStringLiteral("CONTROL DE CALIDAD OBSERVADO"), QStringLiteral("CONTROL FUNCIONAL")};
    r.data.detailUrl = QStringLiteral("http://gesreq.test:7401/greq/publico.do?id=2025175&bandera=1");
    r.importedAt = QDateTime(QDate(2026, 9, 1), QTime(9, 30));
    r.fetchedAt = QDateTime(QDate(2026, 9, 3), QTime(8, 0));
    r.missing = true;
    r.changes = {RequirementChange{QStringLiteral("states"), QStringLiteral("CONTROL CALIDAD ASIGNADO"), QStringLiteral("CONTROL DE CALIDAD OBSERVADO")}};
    r.detail.id = QStringLiteral("2025175");
    r.detail.requestType = QStringLiteral("NUEVA FUNCIONALIDAD");
    r.detail.state = QStringLiteral("CONTROL DE CALIDAD OBSERVADO");
    r.detail.description = QStringLiteral("Alcance:\n• Validar el correo.");
    r.detail.fields = {RequirementField{QStringLiteral("Prioridad"), QStringLiteral("ALTA")}};
    r.detail.sections = {RequirementSection{QStringLiteral("Datos Asignado"), {RequirementField{QStringLiteral("Recurso(s)"), QStringLiteral("LÓPEZ")}}}};
    r.detail.attachments = {RequirementAttachment{QStringLiteral("Archivo de respaldo inicial"), QStringLiteral("req.pdf"), QStringLiteral("http://gesreq.test:7401/greq/docDownload.do?doc=req.pdf")}};
    r.detailFetchedAt = QDateTime(QDate(2026, 9, 3), QTime(8, 5));
    return i;
}
} // namespace

class IssueRepositoryTest : public QObject {
    Q_OBJECT
private slots:
    void roundTripsWhatWasWrittenAndWhatWasImported() {
        QTemporaryDir dir;
        JsonIssueRepository repo(dir.path());
        Issue manual;
        manual.id = QStringLiteral("IS-0008");
        manual.title = QStringLiteral("A mano");
        QVERIFY(repo.saveIssues({fullIssue(), manual}));

        const auto loaded = repo.loadIssues();
        QVERIFY(loaded.has_value());
        QCOMPARE(loaded->size(), 2);
        const Issue& i = loaded->first();
        const Issue expected = fullIssue();
        QCOMPARE(i.id, expected.id);
        QCOMPARE(i.title, expected.title);
        QCOMPARE(i.notes, expected.notes);
        QVERIFY(i.priority == Priority::Alta);
        QVERIFY(i.state == IssueState::Preparing);
        QCOMPARE(i.caseIds, expected.caseIds);
        QCOMPARE(i.planIds, expected.planIds);
        QCOMPARE(i.publication.key, expected.publication.key);
        QCOMPARE(i.publication.url, expected.publication.url);
        QCOMPARE(i.publication.tracker, QStringLiteral("Jira"));
        QCOMPARE(i.publication.project, QStringLiteral("QA"));
        QCOMPARE(i.publication.issueType, QStringLiteral("Tarea"));
        QCOMPARE(i.publication.publishedAt, expected.publication.publishedAt);
        QCOMPARE(i.publication.publishedTitle, expected.publication.publishedTitle);
        QCOMPARE(i.publication.publishedDescription, expected.publication.publishedDescription);
        QCOMPARE(i.publication.status, QStringLiteral("In Progress"));
        QVERIFY(i.publication.uncertain);
        QCOMPARE(i.publication.lastError, QStringLiteral("Host not found"));
        QVERIFY(!loaded->at(1).isPublished());
        QCOMPARE(i.createdAt, expected.createdAt);
        QCOMPARE(i.updatedAt, expected.updatedAt);

        const RequirementLink& r = i.requirement;
        QCOMPARE(r.connection, expected.requirement.connection);
        QCOMPARE(r.data.id, QStringLiteral("2025175"));
        QCOMPARE(r.data.systemCode, QStringLiteral("SUMA TRANSITO"));
        QCOMPARE(r.data.requestedOn, QDate(2025, 5, 15));
        QCOMPARE(r.data.assignedUntil, QDate(2026, 1, 27));
        QCOMPARE(r.data.requester, QStringLiteral("PÉREZ GÓMEZ ANA"));
        QCOMPARE(r.data.states, expected.requirement.data.states);
        QCOMPARE(r.data.detailUrl, expected.requirement.data.detailUrl);
        QCOMPARE(r.importedAt, expected.requirement.importedAt);
        QCOMPARE(r.fetchedAt, expected.requirement.fetchedAt);
        QVERIFY(r.missing);
        QCOMPARE(r.changes.size(), 1);
        QCOMPARE(r.changes[0].after, QStringLiteral("CONTROL DE CALIDAD OBSERVADO"));
        QCOMPARE(r.detail.requestType, QStringLiteral("NUEVA FUNCIONALIDAD"));
        QCOMPARE(r.detail.description, QStringLiteral("Alcance:\n• Validar el correo."));
        QCOMPARE(r.detail.fields.size(), 1);
        QCOMPARE(r.detail.sections.size(), 1);
        QCOMPARE(r.detail.sections[0].fields[0].value, QStringLiteral("LÓPEZ"));
        QCOMPARE(r.detail.attachments.size(), 1);
        QCOMPARE(r.detail.attachments[0].fileName, QStringLiteral("req.pdf"));
        QCOMPARE(r.detailFetchedAt, expected.requirement.detailFetchedAt);

        QVERIFY(!loaded->at(1).isImported());
        QVERIFY(loaded->at(1).requirement.detail.id.isEmpty());
    }

    void withoutAFileThereAreNoIssuesYet() {
        QTemporaryDir dir;
        const auto loaded = JsonIssueRepository(dir.path()).loadIssues();
        QVERIFY(loaded.has_value());
        QVERIFY(loaded->isEmpty());
    }

    // Leer como vacío un fichero dañado haría que el siguiente guardado borrase los issues.
    void aDamagedOrUnknownFileIsNotReadAsEmpty() {
        QTemporaryDir dir;
        const QString path = QDir(dir.path()).filePath(QStringLiteral("issues.json"));
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("{\"version\":1,\"issues\":[");
        }
        QVERIFY(!JsonIssueRepository(dir.path()).loadIssues().has_value());
        {
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
            f.write("{\"version\":2,\"issues\":[]}");
        }
        QVERIFY(!JsonIssueRepository(dir.path()).loadIssues().has_value());
    }
};

QTEST_GUILESS_MAIN(IssueRepositoryTest)
#include "test_issue_repository.moc"
