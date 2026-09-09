// ZephyrClient (infrastructure/testmgmt/) contra un servidor HTTP falso: detección de la ruta de
// la API (ZAPI pública o la del propio plugin), creación del ciclo con sus ids numéricos, estado
// de cada ejecución, veredicto por paso y reparto de las evidencias entre ejecución y paso.

#include "support/FakeHttpServer.h"

#include "infrastructure/testmgmt/ZephyrClient.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

using namespace qaflow;
using qaflow::testing::FakeHttpServer;
using qaflow::testing::HttpRequest;
using qaflow::testing::HttpResponse;

namespace {
TrackerSettings settingsFor(const QString& url) {
    TrackerSettings s;
    s.kind = TrackerKind::Jira;
    s.jiraAuth = JiraAuth::ServerBasic;
    s.url = url;
    s.project = QStringLiteral("SHOP");
    s.user = QStringLiteral("aperez");
    s.token = QStringLiteral("s3creta");
    s.connected = true;
    s.zephyr = true;
    return s;
}

/// Proyecto SHOP = id 13500, con una versión "2.3.0" = id 10100.
void routeProject(FakeHttpServer& server) {
    server.route("GET", "/rest/api/2/project/SHOP", [](const HttpRequest&) {
        return HttpResponse::json(200, "{\"id\":\"13500\",\"key\":\"SHOP\",\"versions\":[{\"id\":\"10100\",\"name\":\"2.3.0\",\"archived\":false}]}");
    });
}

PublishCase caseOf(const QString& id, const QString& testKey, Verdict verdict, const QList<StepResult>& steps) {
    PublishCase c;
    c.caseId = id;
    c.testKey = testKey;
    c.title = QStringLiteral("Checkout");
    c.verdict = verdict;
    for (auto r : steps) {
        RunRecordStep s;
        s.result = r;
        s.note = r == StepResult::Fail ? QStringLiteral("El cupón no descuenta") : QString();
        c.steps << s;
    }
    return c;
}

PublishRequest requestOf(const QList<PublishCase>& cases, const QString& version = QString()) {
    PublishRequest r;
    r.cycleName = QStringLiteral("Regresión Sprint 14 · 12/05/2026");
    r.versionName = version;
    r.description = QStringLiteral("Publicado desde QAflow");
    r.startedAt = QDateTime(QDate(2026, 5, 12), QTime(9, 0));
    r.finishedAt = QDateTime(QDate(2026, 5, 12), QTime(11, 30));
    r.cases = cases;
    return r;
}

QJsonObject bodyOf(const HttpRequest& r) { return QJsonDocument::fromJson(r.body).object(); }

/// El usuario de Jira, de donde sale la configuración regional con la que Zephyr parsea las fechas.
void routeMyself(FakeHttpServer& server, const QByteArray& locale) {
    server.route("GET", "/rest/api/2/myself", [locale](const HttpRequest&) {
        return HttpResponse::json(200, "{\"name\":\"aperez\",\"locale\":\"" + locale + "\"}");
    });
}

/// Como Zephyr: sólo acepta el ciclo si la fecha viene exactamente en el formato de la instalación.
void routeCycleAcceptingOnlyDate(FakeHttpServer& server, const QByteArray& api, const QString& accepted) {
    server.route("POST", api + "/cycle", [accepted](const HttpRequest& r) {
        const QString start = bodyOf(r)[QStringLiteral("startDate")].toString();
        if (!start.isEmpty() && start != accepted)
            return HttpResponse{406, "{\"date\":\"Formato de fecha no v\\u00e1lido. Por favor introduce la fecha con el formato \\\"d/MMM/yy\\\".\"}",
                                "application/json", {}};
        return HttpResponse::json(200, "{\"id\":\"77\",\"responseMessage\":\"Cycle 77 created successfully.\"}");
    });
}

/// El primer POST /cycle de una publicación, que es el que lleva las fechas.
QJsonObject firstCycleBody(const FakeHttpServer& server, const QByteArray& api) {
    for (const auto& r : server.requests)
        if (r.method == "POST" && r.path == api + "/cycle") return bodyOf(r);
    return {};
}

int cycleAttempts(const FakeHttpServer& server, const QByteArray& api) {
    int n = 0;
    for (const auto& r : server.requests)
        if (r.method == "POST" && r.path == api + "/cycle") ++n;
    return n;
}

/// Como Zephyr: si el cliente sólo admite JSON, no hay representación que servir y contesta 406.
FakeHttpServer::Handler onlyForClientsThatAcceptAnything(FakeHttpServer::Handler h) {
    return [h](const HttpRequest& r) {
        if (r.header("accept").contains("*/*")) return h(r);
        return HttpResponse{406, "<html><body>Not Acceptable</body></html>", "text/html", {}};
    };
}

/// Rutas de Zephyr bajo el prefijo dado, con dos resultados de paso por ejecución. Con `strictAccept`
/// se comportan como el Zephyr real, que rechaza a quien pida sólo JSON.
void routeZephyr(FakeHttpServer& server, const QByteArray& api, bool strictAccept = false) {
    auto route = [&](const QByteArray& method, const QByteArray& path, FakeHttpServer::Handler h) {
        server.route(method, path, strictAccept ? onlyForClientsThatAcceptAnything(std::move(h)) : std::move(h));
    };
    route("GET", api + "/cycle", [](const HttpRequest&) { return HttpResponse::json(200, "{}"); });
    route("POST", api + "/cycle", [](const HttpRequest&) { return HttpResponse::json(200, "{\"id\":\"77\",\"responseMessage\":\"Cycle 77 created successfully.\"}"); });
    route("POST", api + "/execution", [](const HttpRequest&) { return HttpResponse::json(200, "{\"501\":{\"id\":501,\"executionStatus\":\"-1\"}}"); });
    route("PUT", api + "/execution/501/execute", [](const HttpRequest&) { return HttpResponse::json(200, "{\"id\":501,\"executionStatus\":\"1\"}"); });
    route("GET", api + "/stepResult", [](const HttpRequest&) { return HttpResponse::json(200, "[{\"id\":9001,\"stepId\":1},{\"id\":9002,\"stepId\":2}]"); });
    route("PUT", api + "/stepResult/9001", [](const HttpRequest&) { return HttpResponse::json(200, "{}"); });
    route("PUT", api + "/stepResult/9002", [](const HttpRequest&) { return HttpResponse::json(200, "{}"); });
    // La subida de evidencias no contesta JSON, sino texto plano.
    route("POST", api + "/attachment", [](const HttpRequest&) { return HttpResponse{200, "Attachment added successfully", "text/plain", {}}; });
    route("GET", "/rest/api/2/issue/SHOP-42", [](const HttpRequest&) { return HttpResponse::json(200, "{\"id\":\"10600\",\"key\":\"SHOP-42\"}"); });
}
} // namespace

class ZephyrClientTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Detección de la ruta de la API -------------------------------------------------------
    void prefersThePublicZapiPathWhenItAnswers() {
        FakeHttpServer server;
        routeProject(server);
        routeZephyr(server, "/rest/zapi/latest");
        ZephyrClient client;
        ConnectionResult out;
        bool done = false;
        client.testConnection(settingsFor(server.baseUrl()), [&](const ConnectionResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(client.apiPath(), QStringLiteral("/rest/zapi/latest"));
    }

    // Zephyr anterior a la 5.6 sin el add-on ZAPI: sólo responde la API del propio plugin.
    void fallsBackToThePluginPathWhenZapiIsNotInstalled() {
        FakeHttpServer server;
        routeProject(server);
        routeZephyr(server, "/rest/zephyr/latest");
        server.route("GET", "/rest/zapi/latest/cycle", [](const HttpRequest&) { return HttpResponse::json(404, "{\"message\":\"Not Found\"}"); });
        ZephyrClient client;
        ConnectionResult out;
        bool done = false;
        client.testConnection(settingsFor(server.baseUrl()), [&](const ConnectionResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(client.apiPath(), QStringLiteral("/rest/zephyr/latest"));
        QVERIFY(out.displayName.contains(QStringLiteral("/rest/zephyr/latest")));
    }

    void reportsWhenNeitherPathAnswers() {
        FakeHttpServer server;
        routeProject(server);
        server.fallback([](const HttpRequest&) { return HttpResponse::json(404, "{\"message\":\"Not Found\"}"); });
        ZephyrClient client;
        ConnectionResult out;
        bool done = false;
        client.testConnection(settingsFor(server.baseUrl()), [&](const ConnectionResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.error.contains(QStringLiteral("Zephyr")));
    }

    // Un 401 no es "esta ruta no existe": se informa en vez de probar la siguiente.
    void authenticationFailureIsNotMistakenForAMissingApi() {
        FakeHttpServer server;
        routeProject(server);
        server.fallback([](const HttpRequest&) { return HttpResponse::json(401, "{\"errorMessages\":[\"unauthorized\"]}"); });
        ZephyrClient client;
        ConnectionResult out;
        bool done = false;
        client.testConnection(settingsFor(server.baseUrl()), [&](const ConnectionResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.error.contains(QStringLiteral("401")));
    }

    // ---- Publicación --------------------------------------------------------------------------
    void publishesCycleExecutionStepsAndAttachments() {
        QTemporaryDir dir;
        const QString shot = dir.filePath(QStringLiteral("cap_001.png"));
        { QFile f(shot); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("PNGDATA"); }
        const QString log = dir.filePath(QStringLiteral("adj_002.log"));
        { QFile f(log); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("LOG"); }

        FakeHttpServer server;
        routeProject(server);
        routeZephyr(server, "/rest/zephyr/latest");
        server.route("GET", "/rest/zapi/latest/cycle", [](const HttpRequest&) { return HttpResponse::json(404, "{}"); });

        PublishCase c = caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Fallido,
                               {StepResult::Pass, StepResult::Fail});
        c.attachments << PublishAttachment{shot, 2}     // evidencia del paso 2
                      << PublishAttachment{log, 0};     // evidencia del caso entero
        c.durationSecs = 245;

        ZephyrClient client;
        PublishResult out;
        bool done = false;
        client.publish(settingsFor(server.baseUrl()), requestOf({c}, QStringLiteral("2.3.0")), [&](const PublishResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.cycleId, QStringLiteral("77"));
        QCOMPARE(out.executions, 1);
        QCOMPARE(out.steps, 2);
        QCOMPARE(out.attachments, 2);
        QVERIFY(out.skipped.isEmpty());

        auto find = [&](const QByteArray& method, const QByteArray& path) {
            for (const auto& r : server.requests) if (r.method == method && r.path.startsWith(path)) return r;
            return HttpRequest{};
        };
        // El ciclo va con los ids numéricos que resolvió de Jira y la fecha en el formato de Zephyr.
        const QJsonObject cycle = bodyOf(find("POST", "/rest/zephyr/latest/cycle"));
        QCOMPARE(cycle[QStringLiteral("projectId")].toString(), QStringLiteral("13500"));
        QCOMPARE(cycle[QStringLiteral("versionId")].toString(), QStringLiteral("10100"));
        QCOMPARE(cycle[QStringLiteral("startDate")].toString(), QStringLiteral("12/May/26"));
        // La ejecución se crea con el id del issue, no con su clave.
        const QJsonObject exec = bodyOf(find("POST", "/rest/zephyr/latest/execution"));
        QCOMPARE(exec[QStringLiteral("issueId")].toString(), QStringLiteral("10600"));
        QCOMPARE(exec[QStringLiteral("cycleId")].toString(), QStringLiteral("77"));
        // Veredicto del caso: fallido = 2.
        QCOMPARE(bodyOf(find("PUT", "/rest/zephyr/latest/execution/501/execute"))[QStringLiteral("status")].toString(), QStringLiteral("2"));
        // Veredicto por paso: 1 el que pasó, 2 el que falló, con su nota.
        QCOMPARE(bodyOf(find("PUT", "/rest/zephyr/latest/stepResult/9001"))[QStringLiteral("status")].toString(), QStringLiteral("1"));
        const QJsonObject step2 = bodyOf(find("PUT", "/rest/zephyr/latest/stepResult/9002"));
        QCOMPARE(step2[QStringLiteral("status")].toString(), QStringLiteral("2"));
        QCOMPARE(step2[QStringLiteral("comment")].toString(), QStringLiteral("El cupón no descuenta"));
        // La evidencia del paso 2 va a su resultado; la del caso, a la ejecución.
        QStringList attachmentPaths;
        for (const auto& r : server.requests) if (r.method == "POST" && r.path.startsWith("/rest/zephyr/latest/attachment")) attachmentPaths << QString::fromUtf8(r.path);
        QCOMPARE(attachmentPaths.size(), 2);
        QVERIFY(attachmentPaths[0].contains(QStringLiteral("entityId=9002")) && attachmentPaths[0].contains(QStringLiteral("entityType=TESTSTEPRESULT")));
        QVERIFY(attachmentPaths[1].contains(QStringLiteral("entityId=501")) && attachmentPaths[1].contains(QStringLiteral("entityType=EXECUTION")));
    }

    // Zephyr no promete JSON en todas sus respuestas: pedir sólo `application/json` tumbaba la
    // publicación entera con un 406 antes de crear nada.
    void publishesAgainstAnApiThatDoesNotAlwaysAnswerJson() {
        QTemporaryDir dir;
        const QString shot = dir.filePath(QStringLiteral("cap_001.png"));
        { QFile f(shot); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("PNGDATA"); }

        FakeHttpServer server;
        routeProject(server);
        routeZephyr(server, "/rest/zapi/latest", /*strictAccept=*/true);

        PublishCase c = caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Superado, {StepResult::Pass});
        c.attachments << PublishAttachment{shot, 1};

        ZephyrClient client;
        PublishResult out;
        bool done = false;
        client.publish(settingsFor(server.baseUrl()), requestOf({c}), [&](const PublishResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.executions, 1);
        QCOMPARE(out.attachments, 1);   // el 200 en texto plano de la subida vale igual
        QVERIFY(out.skipped.isEmpty());
    }

    // ---- Fechas del ciclo ---------------------------------------------------------------------

    // Zephyr parsea la fecha con Joda-Time, que distingue mayúsculas, en la configuración regional
    // del usuario de Jira, y con las abreviaturas de tres letras de `java.locale.providers=COMPAT`.
    void formatsCycleDatesForTheJiraLocale() {
        const QDateTime sept(QDate(2026, 9, 9), QTime(9, 0));
        QCOMPARE(ZephyrClient::cycleDate(sept, QStringLiteral("es_ES")), QStringLiteral("9/sep/26"));
        QCOMPARE(ZephyrClient::cycleDate(sept, QStringLiteral("en_US")), QStringLiteral("9/Sep/26"));
        // Sin configuración regional conocida, en inglés: es como sale un Jira recién instalado.
        QCOMPARE(ZephyrClient::cycleDate(sept, QString()), QStringLiteral("9/Sep/26"));
        QCOMPARE(ZephyrClient::cycleDate(QDateTime(), QStringLiteral("es_ES")), QString());
    }

    // Contra un Jira en español, "9/Sep/26" tumbaba la publicación entera con un 406 antes de crear
    // nada: el ciclo va con la fecha que entiende esa instalación.
    void publishesTheCycleWithTheDateFormatOfTheJiraInstance() {
        FakeHttpServer server;
        routeProject(server);
        routeMyself(server, "es_ES");
        routeZephyr(server, "/rest/zapi/latest");
        routeCycleAcceptingOnlyDate(server, "/rest/zapi/latest", QStringLiteral("9/sep/26"));

        PublishRequest req = requestOf({caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Superado, {StepResult::Pass})});
        req.startedAt = QDateTime(QDate(2026, 9, 9), QTime(9, 0));
        req.finishedAt = QDateTime(QDate(2026, 9, 9), QTime(11, 30));

        ZephyrClient client;
        PublishResult out;
        bool done = false;
        client.publish(settingsFor(server.baseUrl()), req, [&](const PublishResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(firstCycleBody(server, "/rest/zapi/latest")[QStringLiteral("startDate")].toString(), QStringLiteral("9/sep/26"));
        QCOMPARE(cycleAttempts(server, "/rest/zapi/latest"), 1);   // a la primera, sin reintento
    }

    // Cada instalación tiene su formato: si aun así lo rechaza, el ciclo se crea sin fechas en vez
    // de perderse la publicación entera.
    void createsTheCycleWithoutDatesWhenZephyrRejectsThem() {
        FakeHttpServer server;
        routeProject(server);
        routeMyself(server, "es_ES");
        routeZephyr(server, "/rest/zapi/latest");
        routeCycleAcceptingOnlyDate(server, "/rest/zapi/latest", QStringLiteral("no hay formato que valga"));

        ZephyrClient client;
        PublishResult out;
        bool done = false;
        client.publish(settingsFor(server.baseUrl()), requestOf({caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Superado, {StepResult::Pass})}),
                       [&](const PublishResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.cycleId, QStringLiteral("77"));
        QCOMPARE(cycleAttempts(server, "/rest/zapi/latest"), 2);   // con fechas y, al rechazarlas, sin ellas
        const HttpRequest* last = nullptr;
        for (const auto& r : server.requests)
            if (r.method == "POST" && r.path == "/rest/zapi/latest/cycle") last = &r;
        QVERIFY(last);
        QVERIFY(bodyOf(*last)[QStringLiteral("startDate")].toString().isEmpty());
    }

    // Zephyr contesta a los datos inválidos con un 406 y un objeto plano campo→motivo. Sin leerlo,
    // el usuario sólo veía el "Error transferring…" de Qt y no había por dónde empezar.
    void zephyrValidationErrorsReachTheUser() {
        FakeHttpServer server;
        routeProject(server);
        routeMyself(server, "es_ES");
        routeZephyr(server, "/rest/zapi/latest");
        server.route("POST", "/rest/zapi/latest/cycle", [](const HttpRequest&) {
            return HttpResponse{406, "{\"date\":\"Formato de fecha no v\\u00e1lido.\",\"projectId\":\"El ID de proyecto (projectId) es necesario.\"}",
                                "application/json", {}};
        });
        ZephyrClient client;
        PublishResult out;
        bool done = false;
        client.publish(settingsFor(server.baseUrl()), requestOf({caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Superado, {StepResult::Pass})}),
                       [&](const PublishResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY2(out.error.contains(QStringLiteral("Formato de fecha no válido")), qPrintable(out.error));
        QVERIFY2(out.error.contains(QStringLiteral("El ID de proyecto")), qPrintable(out.error));
    }

    // El mensaje de error dice qué petición falló: sin eso, un 406 o un 404 no se pueden situar.
    void errorsNameTheRequestThatFailed() {
        FakeHttpServer server;
        routeProject(server);
        routeZephyr(server, "/rest/zapi/latest");
        server.route("POST", "/rest/zapi/latest/cycle", [](const HttpRequest&) { return HttpResponse{406, "<html>Not Acceptable</html>", "text/html", {}}; });
        ZephyrClient client;
        PublishResult out;
        bool done = false;
        client.publish(settingsFor(server.baseUrl()), requestOf({caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Superado, {StepResult::Pass})}),
                       [&](const PublishResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY2(out.error.contains(QStringLiteral("POST /rest/zapi/latest/cycle")), qPrintable(out.error));
    }

    void casesWithoutTestKeyAreSkippedButTheCycleIsPublished() {
        FakeHttpServer server;
        routeProject(server);
        routeZephyr(server, "/rest/zapi/latest");
        const PublishRequest req = requestOf({caseOf(QStringLiteral("TC-103"), QString(), Verdict::Superado, {StepResult::Pass}),
                                              caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Superado, {StepResult::Pass})});
        ZephyrClient client;
        PublishResult out;
        bool done = false;
        client.publish(settingsFor(server.baseUrl()), req, [&](const PublishResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(out.executions, 1);
        QCOMPARE(out.skipped.size(), 1);
        QVERIFY(out.skipped[0].contains(QStringLiteral("TC-103")));
    }

    void unknownVersionStopsThePublicationBeforeCreatingAnything() {
        FakeHttpServer server;
        routeProject(server);
        routeZephyr(server, "/rest/zapi/latest");
        ZephyrClient client;
        PublishResult out;
        bool done = false;
        client.publish(settingsFor(server.baseUrl()), requestOf({caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Superado, {StepResult::Pass})},
                                                                QStringLiteral("9.9.9")),
                       [&](const PublishResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.error.contains(QStringLiteral("9.9.9")));
        for (const auto& r : server.requests) QVERIFY(r.method != "POST");
    }

    // Una detección fallida dejaba puesta la última ruta que probó: al volver a la instancia que sí
    // tenía Zephyr, se publicaba contra la ruta equivocada sin volver a detectarla.
    void aFailedDetectionDoesNotLeaveARouteCachedForTheNextInstance() {
        FakeHttpServer good;
        routeProject(good);
        routeZephyr(good, "/rest/zapi/latest");

        FakeHttpServer withoutZephyr;
        routeProject(withoutZephyr);
        withoutZephyr.fallback([](const HttpRequest&) { return HttpResponse::json(404, "{\"message\":\"Not Found\"}"); });

        ZephyrClient client;
        // `QTRY_VERIFY` sale de la función con un `return`, así que la espera no puede vivir dentro
        // de una lambda que devuelva el resultado: se recoge por referencia.
        ConnectionResult out;
        bool done = false;
        auto tryConnect = [&](FakeHttpServer& server) {
            done = false;
            client.testConnection(settingsFor(server.baseUrl()), [&](const ConnectionResult& r) { out = r; done = true; });
        };
        tryConnect(good);
        QTRY_VERIFY(done);
        QVERIFY(out.ok);
        QCOMPARE(client.apiPath(), QStringLiteral("/rest/zapi/latest"));
        tryConnect(withoutZephyr);
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY2(client.apiPath().isEmpty(), qPrintable(client.apiPath()));   // no se queda con la que probó

        // Y la instancia buena vuelve a detectarse en vez de heredar la ruta a medias.
        PublishResult published;
        bool publishDone = false;
        client.publish(settingsFor(good.baseUrl()), requestOf({caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Superado, {StepResult::Pass})}),
                       [&](const PublishResult& r) { published = r; publishDone = true; });
        QTRY_VERIFY(publishDone);
        QVERIFY2(published.ok, qPrintable(published.error));
        QCOMPARE(client.apiPath(), QStringLiteral("/rest/zapi/latest"));
    }

    // Que el plugin no esté no se arregla reintentando; que se caiga la red, sí.
    void aMissingPluginIsNotReportedAsWorthRetrying() {
        FakeHttpServer server;
        routeProject(server);
        server.fallback([](const HttpRequest&) { return HttpResponse::json(404, "{\"message\":\"Not Found\"}"); });
        ZephyrClient client;
        PublishResult out;
        bool done = false;
        client.publish(settingsFor(server.baseUrl()), requestOf({caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Superado, {StepResult::Pass})}),
                       [&](const PublishResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(!out.retryable);
    }

    // El Test de Zephyr y el caso de QAflow se editan por separado: si el Test tiene menos pasos,
    // los veredictos que sobran no se pierden en silencio.
    void stepsWithNoCounterpartInTheZephyrTestAreReported() {
        FakeHttpServer server;
        routeProject(server);
        routeZephyr(server, "/rest/zapi/latest");   // el fake devuelve dos resultados de paso
        // Cuatro pasos ejecutados contra un Test de dos.
        const PublishCase c = caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Fallido,
                                     {StepResult::Pass, StepResult::Pass, StepResult::Fail, StepResult::Pass});
        ZephyrClient client;
        PublishResult out;
        bool done = false;
        client.publish(settingsFor(server.baseUrl()), requestOf({c}), [&](const PublishResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.executions, 1);
        QCOMPARE(out.steps, 2);
        QCOMPARE(out.skipped.size(), 1);
        QVERIFY2(out.skipped[0].contains(QStringLiteral("TC-104")) && out.skipped[0].contains(QStringLiteral("SHOP-42")),
                 qPrintable(out.skipped[0]));
    }

    // Una evidencia borrada entre la ejecución y la publicación desaparecía sin dejar rastro.
    void evidenceThatCanNoLongerBeReadIsReported() {
        FakeHttpServer server;
        routeProject(server);
        routeZephyr(server, "/rest/zapi/latest");
        PublishCase c = caseOf(QStringLiteral("TC-104"), QStringLiteral("SHOP-42"), Verdict::Superado, {StepResult::Pass});
        c.attachments << PublishAttachment{QStringLiteral("/no/existe/cap_001.png"), 1};

        ZephyrClient client;
        PublishResult out;
        bool done = false;
        client.publish(settingsFor(server.baseUrl()), requestOf({c}), [&](const PublishResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.attachments, 0);
        QCOMPARE(out.skipped.size(), 1);
        QVERIFY2(out.skipped[0].contains(QStringLiteral("cap_001.png")), qPrintable(out.skipped[0]));
    }

    void statusCodesFollowZephyrsTable() {
        QCOMPARE(ZephyrClient::zephyrStatus(Verdict::Superado), 1);
        QCOMPARE(ZephyrClient::zephyrStatus(Verdict::Fallido), 2);
        QCOMPARE(ZephyrClient::zephyrStatus(Verdict::Bloqueado), 4);
        QCOMPARE(ZephyrClient::zephyrStatus(StepResult::Pass), 1);
        QCOMPARE(ZephyrClient::zephyrStatus(StepResult::Fail), 2);
        QCOMPARE(ZephyrClient::zephyrStatus(StepResult::Block), 4);
        QCOMPARE(ZephyrClient::zephyrStatus(StepResult::Skip), -1);   // N/A queda sin ejecutar
    }
};

QTEST_MAIN(ZephyrClientTest)
#include "test_zephyr_client.moc"
