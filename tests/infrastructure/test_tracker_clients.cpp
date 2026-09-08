// Clientes REST (infrastructure/tracker/): Jira y GitHub contra un servidor HTTP falso en
// localhost (sin literales raw: moc no los entiende). Se comprueba qué se envía (ruta, cabeceras, cuerpo) y cómo se interpretan las
// respuestas, incluidos errores de contenido (4xx, no reintentables), de servidor (5xx) y de red.

#include "support/FakeHttpServer.h"

#include "infrastructure/tracker/GitHubClient.h"
#include "infrastructure/tracker/JiraClient.h"
#include "infrastructure/tracker/TrackerRouter.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace qaflow;
using qaflow::testing::FakeHttpServer;
using qaflow::testing::HttpRequest;
using qaflow::testing::HttpResponse;

namespace {
TrackerSettings jiraSettings(const QString& url) {
    TrackerSettings s;
    s.kind = TrackerKind::Jira; s.url = url; s.project = QStringLiteral("SHOP");
    s.user = QStringLiteral("qa@acme.com"); s.token = QStringLiteral("tok3n"); s.connected = true;
    return s;
}
/// Jira Server 8.5.1: usuario y contraseña, sin PAT (no existen antes de la 8.14).
TrackerSettings jiraServerSettings(const QString& url) {
    TrackerSettings s = jiraSettings(url);
    s.jiraAuth = JiraAuth::ServerBasic;
    s.user = QStringLiteral("aperez"); s.token = QStringLiteral("s3creta");
    return s;
}
TrackerSettings githubSettings(const QString& url) {
    TrackerSettings s;
    s.kind = TrackerKind::GitHub; s.url = url; s.project = QStringLiteral("acme/shop"); s.token = QStringLiteral("ghp_x");
    return s;
}
QJsonObject bodyOf(const HttpRequest& r) { return QJsonDocument::fromJson(r.body).object(); }
} // namespace

class TrackerClientsTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Jira ---------------------------------------------------------------------------------
    void jiraCloudUsesBasicAuthWithEmailAndToken() {
        FakeHttpServer server;
        server.route("GET", "/rest/api/2/myself", [](const HttpRequest&) { return HttpResponse::json(200, "{\"displayName\":\"Ana QA\"}"); });
        JiraClient client;
        ConnectionResult out;
        bool done = false;
        client.testConnection(jiraSettings(server.baseUrl()), [&](const ConnectionResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(out.displayName, QStringLiteral("Ana QA"));
        QCOMPARE(server.requests.size(), 1);
        QCOMPARE(server.requests[0].header("Authorization"), "Basic " + QByteArray("qa@acme.com:tok3n").toBase64());
    }

    void jiraServerTokenUsesBearer() {
        FakeHttpServer server;
        server.route("GET", "/rest/api/2/myself", [](const HttpRequest&) { return HttpResponse::json(200, "{\"displayName\":\"PAT\"}"); });
        JiraClient client;
        TrackerSettings s = jiraSettings(server.baseUrl());
        s.jiraAuth = JiraAuth::ServerToken;
        s.user.clear();
        bool done = false;
        client.testConnection(s, [&](const ConnectionResult&) { done = true; });
        QTRY_VERIFY(done);
        QCOMPARE(server.requests[0].header("Authorization"), QByteArray("Bearer tok3n"));
    }

    // Jira Server / Data Center 8.5.1: Basic auth con usuario y contraseña contra la API v2.
    void jiraServerBasicUsesUserAndPassword() {
        FakeHttpServer server;
        server.route("GET", "/rest/api/2/myself", [](const HttpRequest&) { return HttpResponse::json(200, "{\"displayName\":\"Ana Pérez\"}"); });
        JiraClient client;
        ConnectionResult out;
        bool done = false;
        client.testConnection(jiraServerSettings(server.baseUrl()), [&](const ConnectionResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(out.displayName, QStringLiteral("Ana Pérez"));
        QCOMPARE(server.requests[0].header("Authorization"), "Basic " + QByteArray("aperez:s3creta").toBase64());
    }

    void jiraServerBasicRejectsMissingUser() {
        JiraClient client;
        TrackerSettings s = jiraServerSettings(QStringLiteral("http://127.0.0.1:1"));
        s.user.clear();
        ConnectionResult out;
        client.testConnection(s, [&](const ConnectionResult& r) { out = r; });   // síncrono: no llega a la red
        QVERIFY(!out.ok);
        QVERIFY(!out.error.isEmpty());
    }

    // Tras varios intentos fallidos Jira Server bloquea al usuario y lo dice en sus cabeceras.
    void jiraServerCaptchaChallengeIsExplained() {
        FakeHttpServer server;
        server.fallback([](const HttpRequest&) {
            HttpResponse r = HttpResponse::json(401, "{\"errorMessages\":[]}");
            r.extraHeaders["X-Seraph-LoginReason"] = "AUTHENTICATION_DENIED";
            r.extraHeaders["X-Authentication-Denied-Reason"] = "CAPTCHA_CHALLENGE; login-url=/login.jsp";
            return r;
        });
        JiraClient client;
        ConnectionResult out;
        bool done = false;
        client.testConnection(jiraServerSettings(server.baseUrl()), [&](const ConnectionResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.error.contains(QStringLiteral("CAPTCHA")));
        QVERIFY(out.error.contains(server.baseUrl()));
    }

    void jiraServerWrongPasswordIsExplained() {
        FakeHttpServer server;
        server.fallback([](const HttpRequest&) { return HttpResponse::json(401, "{\"errorMessages\":[]}"); });
        JiraClient client;
        ConnectionResult out;
        bool done = false;
        client.testConnection(jiraServerSettings(server.baseUrl()), [&](const ConnectionResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.error.contains(QStringLiteral("contraseña"), Qt::CaseInsensitive));
    }

    // En Server el asignado va por nombre de usuario; el accountId es exclusivo de Cloud.
    void jiraServerAssignsIssueByUsername() {
        FakeHttpServer server;
        server.route("POST", "/rest/api/2/issue", [](const HttpRequest&) { return HttpResponse::json(201, "{\"key\":\"SHOP-9\"}"); });
        BugReport bug;
        bug.title = QStringLiteral("Fallo al pagar");
        bug.assigneeId = QStringLiteral("aperez");
        JiraClient client;
        IssueResult out;
        bool done = false;
        client.createIssue(jiraServerSettings(server.baseUrl()), bug, [&](const IssueResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        const QJsonObject assignee = bodyOf(server.requests[0])[QStringLiteral("fields")].toObject()[QStringLiteral("assignee")].toObject();
        QCOMPARE(assignee[QStringLiteral("name")].toString(), QStringLiteral("aperez"));
        QVERIFY(!assignee.contains(QStringLiteral("accountId")));
    }

    // El campo "Asignado a" pregunta a Jira: en Server el parámetro es `username`; en Cloud, `query`.
    void jiraServerSearchesAssigneesByUsernameParam() {
        FakeHttpServer server;
        server.route("GET", "/rest/api/2/user/assignable/search", [](const HttpRequest&) {
            return HttpResponse::json(200, "[{\"name\":\"aperez\",\"displayName\":\"Ana Pérez\",\"active\":true},"
                                           "{\"name\":\"jbaja\",\"displayName\":\"Juan Baja\",\"active\":false}]");
        });
        JiraClient client;
        AssigneeSearch out;
        bool done = false;
        client.searchAssignees(jiraServerSettings(server.baseUrl()), QStringLiteral("ana p"), [&](const AssigneeSearch& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(out.assignees.size(), 1);                       // la cuenta desactivada no se puede asignar
        QCOMPARE(out.assignees[0].id, QStringLiteral("aperez"));
        QCOMPARE(out.assignees[0].name, QStringLiteral("Ana Pérez"));
        const QByteArray path = server.requests[0].path;
        QVERIFY(path.contains("project=SHOP"));
        QVERIFY(path.contains("username=ana%20p"));
        QVERIFY(!path.contains("query="));
    }

    void jiraCloudSearchesAssigneesByQueryParam() {
        FakeHttpServer server;
        server.route("GET", "/rest/api/2/user/assignable/search", [](const HttpRequest&) {
            return HttpResponse::json(200, "[{\"accountId\":\"5b10a\",\"displayName\":\"Ana Pérez\"}]");
        });
        JiraClient client;
        AssigneeSearch out;
        bool done = false;
        client.searchAssignees(jiraSettings(server.baseUrl()), QStringLiteral("ana"), [&](const AssigneeSearch& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(out.assignees[0].id, QStringLiteral("5b10a"));
        QVERIFY(server.requests[0].path.contains("query=ana"));
        QVERIFY(!server.requests[0].path.contains("username="));
    }

    void jiraSearchWithoutTextAsksForTheFirstOnesOfTheProject() {
        FakeHttpServer server;
        server.route("GET", "/rest/api/2/user/assignable/search", [](const HttpRequest&) { return HttpResponse::json(200, "[]"); });
        JiraClient client;
        bool done = false;
        client.searchAssignees(jiraServerSettings(server.baseUrl()), QString(), [&](const AssigneeSearch&) { done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!server.requests[0].path.contains("username="));
        QVERIFY(server.requests[0].path.contains("maxResults="));
    }

    void jiraSearchReportsTheServerError() {
        FakeHttpServer server;
        server.fallback([](const HttpRequest&) { return HttpResponse::json(401, "{\"errorMessages\":[]}"); });
        JiraClient client;
        AssigneeSearch out;
        bool done = false;
        client.searchAssignees(jiraServerSettings(server.baseUrl()), QStringLiteral("ana"), [&](const AssigneeSearch& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.error.contains(QStringLiteral("contraseña"), Qt::CaseInsensitive));
    }

    void jiraServerMetadataIdentifiesAssigneesByUsername() {
        FakeHttpServer server;
        server.route("GET", "/rest/api/2/project/SHOP", [](const HttpRequest&) { return HttpResponse::json(200, "{\"issueTypes\":[],\"components\":[],\"versions\":[]}"); });
        server.route("GET", "/rest/api/2/priority", [](const HttpRequest&) { return HttpResponse::json(200, "[]"); });
        server.route("GET", "/rest/api/2/user/assignable/search", [](const HttpRequest&) {
            return HttpResponse::json(200, "[{\"name\":\"aperez\",\"key\":\"aperez\",\"displayName\":\"Ana Pérez\"}]");
        });
        JiraClient client;
        MetadataResult out;
        bool done = false;
        client.fetchMetadata(jiraServerSettings(server.baseUrl()), [&](const MetadataResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(out.metadata.assignees.size(), 1);
        QCOMPARE(out.metadata.assignees[0].id, QStringLiteral("aperez"));
        QCOMPARE(out.metadata.assignees[0].name, QStringLiteral("Ana Pérez"));
    }

    void jiraTestConnectionRejectsMissingToken() {
        JiraClient client;
        TrackerSettings s = jiraSettings(QStringLiteral("http://127.0.0.1:1"));
        s.token.clear();
        ConnectionResult out;
        client.testConnection(s, [&](const ConnectionResult& r) { out = r; });   // síncrono: no llega a la red
        QVERIFY(!out.ok);
        QVERIFY(!out.error.isEmpty());
    }

    void jiraCreateIssueSendsMappedFieldsAndUploadsAttachments() {
        QTemporaryDir dir;
        const QString shot = dir.filePath(QStringLiteral("cap_001.png"));
        { QFile f(shot); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("PNGDATA"); }

        FakeHttpServer server;
        server.route("POST", "/rest/api/2/issue", [](const HttpRequest&) { return HttpResponse::json(201, "{\"id\":\"1\",\"key\":\"SHOP-143\"}"); });
        server.route("POST", "/rest/api/2/issue/SHOP-143/attachments", [](const HttpRequest&) { return HttpResponse::json(200, "[]"); });

        BugReport bug;
        bug.title = QStringLiteral("El cupón no descuenta");
        bug.actual = QStringLiteral("Total sin cambios");
        bug.linkedCaseId = QStringLiteral("TC-104");
        bug.priority = QStringLiteral("High");
        bug.assigneeId = QStringLiteral("acc-123");
        bug.components = {QStringLiteral("Carrito")};
        bug.affectsVersions = {QStringLiteral("2.3")};
        bug.labels = {QStringLiteral("regresión sprint")};
        bug.attachmentPaths = {shot, dir.filePath(QStringLiteral("missing.png"))};

        JiraClient client;
        IssueResult out;
        bool done = false;
        client.createIssue(jiraSettings(server.baseUrl()), bug, [&](const IssueResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(out.key, QStringLiteral("SHOP-143"));
        QCOMPARE(out.url, server.baseUrl() + QStringLiteral("/browse/SHOP-143"));
        QCOMPARE(out.attachmentsUploaded, 1);   // el fichero inexistente se omite

        QCOMPARE(server.requests.size(), 2);
        const QJsonObject fields = bodyOf(server.requests[0])[QStringLiteral("fields")].toObject();
        QCOMPARE(fields[QStringLiteral("project")].toObject()[QStringLiteral("key")].toString(), QStringLiteral("SHOP"));
        QCOMPARE(fields[QStringLiteral("issuetype")].toObject()[QStringLiteral("name")].toString(), QStringLiteral("Bug"));
        QCOMPARE(fields[QStringLiteral("summary")].toString(), bug.title);
        QCOMPARE(fields[QStringLiteral("priority")].toObject()[QStringLiteral("name")].toString(), QStringLiteral("High"));
        QCOMPARE(fields[QStringLiteral("assignee")].toObject()[QStringLiteral("accountId")].toString(), QStringLiteral("acc-123"));   // Cloud: accountId
        QCOMPARE(fields[QStringLiteral("components")].toArray().first().toObject()[QStringLiteral("name")].toString(), QStringLiteral("Carrito"));
        QCOMPARE(fields[QStringLiteral("versions")].toArray().first().toObject()[QStringLiteral("name")].toString(), QStringLiteral("2.3"));
        const QJsonArray labels = fields[QStringLiteral("labels")].toArray();
        QVERIFY(labels.contains(QJsonValue(QStringLiteral("qaflow"))));
        QVERIFY(labels.contains(QJsonValue(QStringLiteral("TC-104"))));
        QVERIFY(labels.contains(QJsonValue(QStringLiteral("regresión-sprint"))));   // sin espacios
        QVERIFY(fields[QStringLiteral("description")].toString().contains(QStringLiteral("Total sin cambios")));

        const HttpRequest& upload = server.requests[1];
        QVERIFY(upload.header("Content-Type").startsWith("multipart/form-data"));
        QCOMPARE(upload.header("X-Atlassian-Token"), QByteArray("no-check"));
        QVERIFY(upload.body.contains("filename=\"cap_001.png\""));
        QVERIFY(upload.body.contains("PNGDATA"));
    }

    void jiraContentRejectionIsNotRetryable() {
        FakeHttpServer server;
        server.route("POST", "/rest/api/2/issue", [](const HttpRequest&) {
            return HttpResponse::json(400, "{\"errorMessages\":[],\"errors\":{\"components\":\"Component name 'X' is not valid\"}}");
        });
        JiraClient client;
        BugReport bug; bug.title = QStringLiteral("t"); bug.actual = QStringLiteral("a");
        IssueResult out; bool done = false;
        client.createIssue(jiraSettings(server.baseUrl()), bug, [&](const IssueResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(!out.retryable);
        QVERIFY(out.error.contains(QStringLiteral("HTTP 400")));
        QVERIFY(out.error.contains(QStringLiteral("components: Component name 'X' is not valid")));
    }

    void serverErrorAndConnectionRefusedAreRetryable() {
        FakeHttpServer server;
        server.route("POST", "/rest/api/2/issue", [](const HttpRequest&) { return HttpResponse::json(503, "{\"message\":\"maintenance\"}"); });
        JiraClient client;
        BugReport bug; bug.title = QStringLiteral("t"); bug.actual = QStringLiteral("a");
        IssueResult out; bool done = false;
        client.createIssue(jiraSettings(server.baseUrl()), bug, [&](const IssueResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.retryable);

        // Puerto cerrado: fallo de red
        const QString deadUrl = server.baseUrl();
        server.close();
        done = false;
        client.createIssue(jiraSettings(deadUrl), bug, [&](const IssueResult& r) { out = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(!out.ok);
        QVERIFY(out.retryable);
        QVERIFY(!out.error.startsWith(QStringLiteral("HTTP")));
    }

    void jiraFetchStatusUsesStatusCategory() {
        FakeHttpServer server;
        server.route("GET", "/rest/api/2/issue/SHOP-143", [](const HttpRequest& r) {
            if (!r.path.contains("fields=status")) return HttpResponse::json(400, "{}");
            return HttpResponse::json(200, "{\"fields\":{\"status\":{\"name\":\"Done\",\"statusCategory\":{\"key\":\"done\"}}}}");
        });
        JiraClient client;
        IssueStatus out; bool done = false;
        client.fetchStatus(jiraSettings(server.baseUrl()), QStringLiteral("SHOP-143"), [&](const IssueStatus& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(out.status, QStringLiteral("Done"));
        QVERIFY(out.resolved);
    }

    void jiraFetchMetadataChainsThreeRequests() {
        FakeHttpServer server;
        server.route("GET", "/rest/api/2/project/SHOP", [](const HttpRequest&) {
            return HttpResponse::json(200, "{\"issueTypes\":[{\"name\":\"Bug\",\"subtask\":false},{\"name\":\"Sub-task\",\"subtask\":true}],\"components\":[{\"name\":\"Carrito\"}],\"versions\":[{\"name\":\"2.3\",\"archived\":false},{\"name\":\"1.0\",\"archived\":true}]}");
        });
        server.route("GET", "/rest/api/2/priority", [](const HttpRequest&) { return HttpResponse::json(200, "[{\"name\":\"High\"},{\"name\":\"Low\"}]"); });
        server.route("GET", "/rest/api/2/user/assignable/search", [](const HttpRequest&) {
            return HttpResponse::json(200, "[{\"accountId\":\"acc-1\",\"displayName\":\"Ana\",\"name\":\"ana\"}]");
        });
        JiraClient client;
        MetadataResult out; bool done = false;
        client.fetchMetadata(jiraSettings(server.baseUrl()), [&](const MetadataResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(out.metadata.issueTypes, QStringList{QStringLiteral("Bug")});          // sin subtareas
        QCOMPARE(out.metadata.versions, QStringList{QStringLiteral("2.3")});            // sin archivadas
        QCOMPARE(out.metadata.priorities, (QStringList{QStringLiteral("High"), QStringLiteral("Low")}));
        QCOMPARE(out.metadata.assignees.size(), 1);
        QCOMPARE(out.metadata.assignees[0].id, QStringLiteral("acc-1"));                // Cloud: accountId
        QCOMPARE(server.requests.size(), 3);
    }

    // ---- GitHub -------------------------------------------------------------------------------
    void githubCreateIssueUsesLabelsAndListsAttachmentsByName() {
        QTemporaryDir dir;
        const QString shot = dir.filePath(QStringLiteral("cap_007.png"));
        { QFile f(shot); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("x"); }
        FakeHttpServer server;
        server.route("POST", "/repos/acme/shop/issues", [](const HttpRequest&) {
            return HttpResponse::json(201, "{\"number\":42,\"html_url\":\"https://github.com/acme/shop/issues/42\"}");
        });
        BugReport bug;
        bug.title = QStringLiteral("Crash al pagar"); bug.actual = QStringLiteral("500");
        bug.linkedCaseId = QStringLiteral("TC-104"); bug.components = {QStringLiteral("checkout")};
        bug.priority = QStringLiteral("priority: high"); bug.assigneeId = QStringLiteral("octocat");
        bug.attachmentPaths = {shot};
        GitHubClient client;
        IssueResult out; bool done = false;
        client.createIssue(githubSettings(server.baseUrl()), bug, [&](const IssueResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(out.key, QStringLiteral("#42"));
        QCOMPARE(out.url, QStringLiteral("https://github.com/acme/shop/issues/42"));
        QCOMPARE(out.attachmentsUploaded, 0);
        QCOMPARE(server.requests.size(), 1);   // la API no admite adjuntos: ninguna subida
        const HttpRequest& req = server.requests[0];
        QCOMPARE(req.header("Authorization"), QByteArray("Bearer ghp_x"));
        QCOMPARE(req.header("Accept"), QByteArray("application/vnd.github+json"));
        const QJsonObject body = bodyOf(req);
        const QJsonArray labels = body[QStringLiteral("labels")].toArray();
        QVERIFY(labels.contains(QJsonValue(QStringLiteral("checkout"))));
        QVERIFY(labels.contains(QJsonValue(QStringLiteral("priority: high"))));
        QCOMPARE(body[QStringLiteral("assignees")].toArray().first().toString(), QStringLiteral("octocat"));
        QVERIFY(body[QStringLiteral("body")].toString().contains(QStringLiteral("cap_007.png")));
    }

    void githubFetchStatusMapsClosedToResolved() {
        FakeHttpServer server;
        server.route("GET", "/repos/acme/shop/issues/42", [](const HttpRequest&) { return HttpResponse::json(200, "{\"state\":\"closed\"}"); });
        GitHubClient client;
        IssueStatus out; bool done = false;
        client.fetchStatus(githubSettings(server.baseUrl()), QStringLiteral("#42"), [&](const IssueStatus& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QVERIFY(out.resolved);
        QCOMPARE(out.status, QStringLiteral("closed"));
    }

    // ---- Router -------------------------------------------------------------------------------
    void routerDispatchesByTrackerKind() {
        FakeHttpServer server;
        server.route("GET", "/user", [](const HttpRequest&) { return HttpResponse::json(200, "{\"login\":\"octocat\"}"); });
        server.route("GET", "/rest/api/2/myself", [](const HttpRequest&) { return HttpResponse::json(200, "{\"displayName\":\"Ana\"}"); });
        TrackerRouter router;
        ConnectionResult gh, jira; bool d1 = false, d2 = false;
        router.testConnection(githubSettings(server.baseUrl()), [&](const ConnectionResult& r) { gh = r; d1 = true; });
        router.testConnection(jiraSettings(server.baseUrl()), [&](const ConnectionResult& r) { jira = r; d2 = true; });
        QTRY_VERIFY(d1 && d2);
        QCOMPARE(gh.displayName, QStringLiteral("octocat"));
        QCOMPARE(jira.displayName, QStringLiteral("Ana"));
    }
};

QTEST_MAIN(TrackerClientsTest)
#include "test_tracker_clients.moc"
