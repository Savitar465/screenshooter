// GesreqClient (infrastructure/requirements/) contra un GESREQ falso en localhost que se comporta como
// el real: la sesión va en una cookie, el login es un formulario que redirige al entrar y, sin sesión,
// las páginas responden 200 con el formulario de login (bandeja) o con la ficha vacía (detalle). Se
// comprueba que nada de eso llega como una bandeja vacía o un requerimiento en blanco.
//
// `readsTheInboxOfARealGesreq` lee además la bandeja de un GESREQ de verdad si se le dice dónde:
//   QAFLOW_GESREQ_URL=http://servidor:7401/greq QAFLOW_GESREQ_USER=… QAFLOW_GESREQ_PASSWORD=…
//       ctest --test-dir build -R gesreq_client --output-on-failure -V

#include "support/FakeHttpServer.h"

#include "infrastructure/requirements/GesreqClient.h"

#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QUrlQuery>
#include <QtTest>

using namespace qaflow;
using qaflow::testing::FakeHttpServer;
using qaflow::testing::HttpRequest;
using qaflow::testing::HttpResponse;

namespace {
const QString kUser = QStringLiteral("QAUSR0101");
const QString kPassword = QStringLiteral("s3creta+1");   // con '+': en un formulario tiene que viajar como %2B

QByteArray fixture(const char* name) {
    QFile f(QString::fromUtf8(QAFLOW_FIXTURES_DIR "/gesreq/") + QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly)) qFatal("No se encuentra el fixture %s", name);
    return f.readAll();
}

HttpResponse page(const QByteArray& html) { return HttpResponse{200, html, "text/html;charset=utf-8", {}}; }

QByteArray sessionCookie(const HttpRequest& r) {
    static const QRegularExpression cookie(QStringLiteral("JSESSIONID=([^;\\s]+)"));
    return cookie.match(QString::fromLatin1(r.header("cookie"))).captured(1).toLatin1();
}

/// GESREQ falso: la página de entrada abre una sesión con su cookie, el login la autentica si usuario y
/// clave son los buenos y redirige al dashboard, y las demás páginas sólo sirven datos a una sesión
/// autenticada. `keepsSessions` a false simula un servidor que olvida la sesión nada más entrar.
class FakeGesreq {
public:
    FakeHttpServer server;
    QByteArray inbox = fixture("bandeja_calidad.html");
    QByteArray tracking = fixture("sistemas.html");     // poai.do: el informe de seguimiento, con el catálogo de sistemas
    QByteArray additional = fixture("sistemas.html");   // registroadicional.do: el mismo catálogo
    QSet<QByteArray> authenticated;
    bool keepsSessions = true;

    FakeGesreq() {
        const QByteArray login = fixture("login.html");
        const QByteArray detail = fixture("detalle.html");
        const QByteArray empty = fixture("detalle_vacio.html");
        server.route("GET", "/greq/", [this, login](const HttpRequest&) {
            HttpResponse res = page(login);
            res.extraHeaders.insert("Set-Cookie", "JSESSIONID=s" + QByteArray::number(++m_opened) + "; path=/; HttpOnly");
            return res;
        });
        server.route("POST", "/greq/login.do", [this, login](const HttpRequest& r) {
            const QUrlQuery form(QString::fromLatin1(r.body));
            const QByteArray session = sessionCookie(r);
            if (session.isEmpty() || form.queryItemValue(QStringLiteral("usuario"), QUrl::FullyDecoded) != kUser
                || form.queryItemValue(QStringLiteral("clave"), QUrl::FullyDecoded) != kPassword)
                return page(login);
            authenticated.insert(session);
            HttpResponse res{302, QByteArray(), "text/html", {}};
            res.extraHeaders.insert("Location", "/greq/dashboard.do");
            return res;
        });
        server.route("GET", "/greq/dashboard.do", [this, login](const HttpRequest& r) {
            return page(authenticated.contains(sessionCookie(r)) ? inbox : login);
        });
        server.route("GET", "/greq/calidadreg.do", [this, login](const HttpRequest& r) { return page(signedIn(r) ? inbox : login); });
        server.route("GET", "/greq/publico.do", [this, detail, empty](const HttpRequest& r) {
            // Sin sesión, la cáscara vacía; con sesión, un número que no existe trae su ficha sin datos.
            static const QByteArray missing = fixture("detalle_inexistente.html");
            if (!signedIn(r)) return page(empty);
            return page(r.path.contains("id=2025101&") ? detail : missing);
        });
        server.route("GET", "/greq/poai.do", [this, login](const HttpRequest& r) { return page(signedIn(r) ? tracking : login); });
        server.route("GET", "/greq/registroadicional.do", [this, login](const HttpRequest& r) { return page(signedIn(r) ? additional : login); });
    }

    RequirementSourceSettings settings(const QString& password = kPassword) const {
        RequirementSourceSettings s;
        s.url = server.baseUrl() + QStringLiteral("/greq");
        s.user = kUser;
        s.password = password;
        return s;
    }
    void expireSessions() { authenticated.clear(); }
    int count(const QByteArray& method, const QByteArray& path) const {
        return int(std::count_if(server.requests.cbegin(), server.requests.cend(), [&](const HttpRequest& r) {
            return r.method == method && r.path.left(r.path.indexOf('?') < 0 ? r.path.size() : r.path.indexOf('?')) == path;
        }));
    }
    int logins() const { return count("POST", "/greq/login.do"); }

private:
    bool signedIn(const HttpRequest& r) const { return keepsSessions && authenticated.contains(sessionCookie(r)); }
    int m_opened = 0;
};
} // namespace

class GesreqClientTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Sesión -------------------------------------------------------------------------------
    void logsInWithTheFormAndReadsTheInbox() {
        FakeGesreq gesreq;
        GesreqClient client;
        RequirementInboxResult out;
        bool done = false;
        client.fetchInbox(gesreq.settings(), [&](const RequirementInboxResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.requirements.size(), 3);
        QCOMPARE(out.requirements[0].id, QStringLiteral("2025101"));
        QVERIFY(out.fetchedAt.isValid());
        QVERIFY(client.hasSession(gesreq.settings()));

        QCOMPARE(gesreq.logins(), 1);
        for (const auto& r : gesreq.server.requests) {
            if (r.method != "POST") continue;
            // Como lo envía un navegador, y con la contraseña codificada entera.
            QCOMPARE(r.header("content-type"), QByteArray("application/x-www-form-urlencoded"));
            QVERIFY2(r.body.contains("usuario=QAUSR0101") && r.body.contains("clave=s3creta%2B1"), r.body.constData());
            QVERIFY(!sessionCookie(r).isEmpty());   // el formulario va dentro de la sesión abierta por la entrada
        }
    }

    void reusesTheSessionAcrossRequests() {
        FakeGesreq gesreq;
        GesreqClient client;
        int finished = 0;
        client.fetchInbox(gesreq.settings(), [&](const RequirementInboxResult& r) { QVERIFY(r.ok); ++finished; });
        QTRY_COMPARE(finished, 1);
        client.fetchInbox(gesreq.settings(), [&](const RequirementInboxResult& r) { QVERIFY(r.ok); ++finished; });
        client.fetchDetail(gesreq.settings(), QStringLiteral("2025101"), [&](const RequirementDetailResult& r) { QVERIFY(r.ok); ++finished; });
        QTRY_COMPARE(finished, 3);
        QCOMPARE(gesreq.logins(), 1);
    }

    // Dos lecturas a la vez sin sesión esperan al mismo login en vez de lanzar dos que se pisarían la cookie.
    void concurrentRequestsShareOneLogin() {
        FakeGesreq gesreq;
        GesreqClient client;
        RequirementInboxResult inbox;
        RequirementDetailResult detail;
        int finished = 0;
        client.fetchInbox(gesreq.settings(), [&](const RequirementInboxResult& r) { inbox = r; ++finished; });
        client.fetchDetail(gesreq.settings(), QStringLiteral("2025101"), [&](const RequirementDetailResult& r) { detail = r; ++finished; });
        QTRY_COMPARE(finished, 2);
        QVERIFY2(inbox.ok, qPrintable(inbox.error));
        QVERIFY2(detail.ok, qPrintable(detail.error));
        QCOMPARE(gesreq.logins(), 1);
    }

    void rejectedCredentialsAreAnErrorNotAnEmptyInbox() {
        FakeGesreq gesreq;
        GesreqClient client;
        RequirementInboxResult out;
        bool done = false;
        client.fetchInbox(gesreq.settings(QStringLiteral("mala")), [&](const RequirementInboxResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.failure == RequirementSourceFailure::Credentials);
        QVERIFY(!out.retryable());
        QVERIFY2(out.error.contains(QStringLiteral("contraseña")), qPrintable(out.error));
        QCOMPARE(gesreq.count("GET", "/greq/calidadreg.do"), 0);   // sin sesión no se pide nada más
        QVERIFY(!client.hasSession(gesreq.settings()));
    }

    // GESREQ contesta 200 con el formulario de login cuando la sesión caducó: se entra otra vez y se repite.
    void anExpiredSessionLogsInAgainOnce() {
        FakeGesreq gesreq;
        GesreqClient client;
        RequirementInboxResult out;
        bool done = false;
        client.fetchInbox(gesreq.settings(), [&](const RequirementInboxResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.ok);

        gesreq.expireSessions();
        done = false;
        client.fetchInbox(gesreq.settings(), [&](const RequirementInboxResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.requirements.size(), 3);
        QCOMPARE(gesreq.logins(), 2);
        QCOMPARE(gesreq.count("GET", "/greq/calidadreg.do"), 3);   // la primera, la que vio el login y su repetición
    }

    // En el detalle, la sesión caducada no trae el login sino la ficha vacía: tampoco es un requerimiento en blanco.
    void anExpiredSessionInTheDetailLogsInAgainInsteadOfReturningABlankRequirement() {
        FakeGesreq gesreq;
        GesreqClient client;
        bool done = false;
        client.fetchInbox(gesreq.settings(), [&](const RequirementInboxResult&) { done = true; });
        QTRY_VERIFY(done);

        gesreq.expireSessions();
        RequirementDetailResult out;
        done = false;
        client.fetchDetail(gesreq.settings(), QStringLiteral("2025101"), [&](const RequirementDetailResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.detail.id, QStringLiteral("2025101"));
        QCOMPARE(gesreq.logins(), 2);
    }

    // Un servidor que olvida la sesión nada más entrar no puede tener al cliente entrando en bucle.
    void aSessionThatIsNotKeptIsReportedInsteadOfLooping() {
        FakeGesreq gesreq;
        gesreq.keepsSessions = false;
        GesreqClient client;
        RequirementInboxResult out;
        bool done = false;
        client.fetchInbox(gesreq.settings(), [&](const RequirementInboxResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.failure == RequirementSourceFailure::Credentials);
        QVERIFY2(out.error.contains(QStringLiteral("sesión")), qPrintable(out.error));
        QCOMPARE(gesreq.logins(), 1);
    }

    void changingTheUserNeedsAnotherSession() {
        FakeGesreq gesreq;
        GesreqClient client;
        bool done = false;
        client.fetchInbox(gesreq.settings(), [&](const RequirementInboxResult&) { done = true; });
        QTRY_VERIFY(done);
        RequirementSourceSettings other = gesreq.settings();
        other.user = QStringLiteral("OTROUSR");
        QVERIFY(client.hasSession(gesreq.settings()));
        QVERIFY(!client.hasSession(other));

        RequirementInboxResult out;
        done = false;
        client.fetchInbox(other, [&](const RequirementInboxResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.failure == RequirementSourceFailure::Credentials);   // no hereda la sesión del otro usuario
        QCOMPARE(gesreq.logins(), 2);
    }

    // ---- Detalle -------------------------------------------------------------------------------
    void readsTheDetailOfARequirement() {
        FakeGesreq gesreq;
        GesreqClient client;
        RequirementDetailResult out;
        bool done = false;
        client.fetchDetail(gesreq.settings(), QStringLiteral("2025101"), [&](const RequirementDetailResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.detail.systemCode, QStringLiteral("PORTAL WEB"));
        QCOMPARE(out.detail.state, QStringLiteral("CONTROL DE CALIDAD OBSERVADO"));
        QCOMPARE(out.detail.attachments.size(), 2);
        QCOMPARE(out.detail.attachments[0].url, gesreq.server.baseUrl() + QStringLiteral("/greq/docDownload.do?doc=2025/2025101REQ20250314183739.pdf"));
    }

    // Con sesión, un número que no existe trae su ficha sin datos: es NotFound en el acto, sin renovar
    // la sesión (eso es para la cáscara vacía que llega sin ella).
    void aRequirementThatDoesNotExistIsNotFound() {
        FakeGesreq gesreq;
        GesreqClient client;
        bool done = false;
        client.fetchInbox(gesreq.settings(), [&](const RequirementInboxResult&) { done = true; });
        QTRY_VERIFY(done);

        RequirementDetailResult out;
        done = false;
        client.fetchDetail(gesreq.settings(), QStringLiteral("2025999"), [&](const RequirementDetailResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.failure == RequirementSourceFailure::NotFound);
        QVERIFY2(out.error.contains(QStringLiteral("2025999")), qPrintable(out.error));
        QCOMPARE(gesreq.logins(), 1);
        QCOMPARE(gesreq.count("GET", "/greq/publico.do"), 1);
    }

    void anIdThatIsNotANumberIsRejectedWithoutAsking() {
        FakeGesreq gesreq;
        GesreqClient client;
        RequirementDetailResult out;
        bool done = false;
        client.fetchDetail(gesreq.settings(), QStringLiteral("2025101&accion=calidadreg"), [&](const RequirementDetailResult& r) { out = r; done = true; });
        QVERIFY(done);   // sin red: contesta en el acto
        QVERIFY(out.failure == RequirementSourceFailure::NotFound);
        QVERIFY(gesreq.server.requests.isEmpty());
    }

    // ---- Catálogo de sistemas ------------------------------------------------------------------
    void readsTheCatalogOfSystems() {
        FakeGesreq gesreq;
        GesreqClient client;
        RequirementSystemsResult out;
        bool done = false;
        client.fetchSystems(gesreq.settings(), [&](const RequirementSystemsResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.systems.size(), 5);
        QCOMPARE(out.systems[2].code, QStringLiteral("PORTAL WEB"));
        QCOMPARE(gesreq.count("GET", "/greq/poai.do"), 1);
        QCOMPARE(gesreq.count("GET", "/greq/registroadicional.do"), 0);
    }

    // Quien no ve el informe de seguimiento sigue teniendo el catálogo en el registro adicional.
    void looksForTheCatalogInTheNextPageWhenThePreferredOneLacksIt() {
        FakeGesreq gesreq;
        gesreq.tracking = gesreq.inbox;   // una página de la aplicación, pero sin el desplegable
        GesreqClient client;
        RequirementSystemsResult out;
        bool done = false;
        client.fetchSystems(gesreq.settings(), [&](const RequirementSystemsResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(out.systems.size(), 5);
        QCOMPARE(gesreq.count("GET", "/greq/registroadicional.do"), 1);
    }

    void aCatalogThatIsNowhereIsAPageChange() {
        FakeGesreq gesreq;
        gesreq.tracking = gesreq.inbox;
        gesreq.additional = gesreq.inbox;
        GesreqClient client;
        RequirementSystemsResult out;
        bool done = false;
        client.fetchSystems(gesreq.settings(), [&](const RequirementSystemsResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.systems.isEmpty());
        QVERIFY(out.failure == RequirementSourceFailure::PageChanged);
        QVERIFY2(out.error.contains(QStringLiteral("sistemas")), qPrintable(out.error));
    }

    // ---- Errores -------------------------------------------------------------------------------
    void aChangedInboxPageIsReportedAsSuch() {
        FakeGesreq gesreq;
        gesreq.inbox.replace(QByteArray("id=\"main-table\""), QByteArray("id=\"tabla-nueva\""));
        GesreqClient client;
        RequirementInboxResult out;
        bool done = false;
        client.fetchInbox(gesreq.settings(), [&](const RequirementInboxResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.failure == RequirementSourceFailure::PageChanged);
        QVERIFY2(out.error.contains(QStringLiteral("estructura")), qPrintable(out.error));
    }

    void missingSettingsFailBeforeAnyRequest() {
        FakeGesreq gesreq;
        GesreqClient client;
        RequirementInboxResult out;
        bool done = false;
        client.fetchInbox(gesreq.settings(QString()), [&](const RequirementInboxResult& r) { out = r; done = true; });
        QVERIFY(done);
        QVERIFY(out.failure == RequirementSourceFailure::Configuration);
        QVERIFY(gesreq.server.requests.isEmpty());
    }

    void anUnreachableServerIsWorthRetrying() {
        FakeGesreq gesreq;
        const RequirementSourceSettings s = gesreq.settings();
        gesreq.server.close();
        GesreqClient client;
        RequirementInboxResult out;
        bool done = false;
        client.fetchInbox(s, [&](const RequirementInboxResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.failure == RequirementSourceFailure::Network);
        QVERIFY(out.retryable());
    }

    void aWrongAddressIsAConfigurationProblem() {
        FakeGesreq gesreq;
        RequirementSourceSettings s = gesreq.settings();
        s.url = gesreq.server.baseUrl() + QStringLiteral("/otra");
        GesreqClient client;
        RequirementInboxResult out;
        bool done = false;
        client.fetchInbox(s, [&](const RequirementInboxResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.failure == RequirementSourceFailure::Configuration);
        QVERIFY(!out.retryable());
        QVERIFY2(out.error.contains(QStringLiteral("404")), qPrintable(out.error));
    }

    // ---- Conexión ------------------------------------------------------------------------------
    void testConnectionSaysWhoSignedInAndHowManyRequirements() {
        FakeGesreq gesreq;
        GesreqClient client;
        ConnectionResult out;
        bool done = false;
        client.testConnection(gesreq.settings(), [&](const ConnectionResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QVERIFY2(out.displayName.contains(QStringLiteral("Tester Uno, Ana")), qPrintable(out.displayName));
        QVERIFY2(out.displayName.contains(QStringLiteral("3 requerimientos")), qPrintable(out.displayName));
    }

    // ---- GESREQ real (opcional) ----------------------------------------------------------------
    void readsTheInboxOfARealGesreq() {
        RequirementSourceSettings s;
        s.url = qEnvironmentVariable("QAFLOW_GESREQ_URL");
        s.user = qEnvironmentVariable("QAFLOW_GESREQ_USER");
        s.password = qEnvironmentVariable("QAFLOW_GESREQ_PASSWORD");
        if (s.url.isEmpty()) QSKIP("Sin QAFLOW_GESREQ_URL no se prueba contra un GESREQ real");

        GesreqClient client;
        ConnectionResult connection;
        bool done = false;
        client.testConnection(s, [&](const ConnectionResult& r) { connection = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 60000);
        QVERIFY2(connection.ok, qPrintable(connection.error));
        qInfo().noquote() << "Conexión:" << connection.displayName;

        RequirementInboxResult inbox;
        done = false;
        client.fetchInbox(s, [&](const RequirementInboxResult& r) { inbox = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 60000);
        QVERIFY2(inbox.ok, qPrintable(inbox.error));
        for (const auto& r : inbox.requirements)
            qInfo().noquote() << r.id << "·" << r.systemCode << "·" << r.priority << "·" << r.states.join(QStringLiteral(" + "))
                              << "·" << r.assignedFrom.toString(Qt::ISODate) << "→" << r.assignedUntil.toString(Qt::ISODate);
        RequirementSystemsResult systems;
        done = false;
        client.fetchSystems(s, [&](const RequirementSystemsResult& r) { systems = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 60000);
        QVERIFY2(systems.ok, qPrintable(systems.error));
        qInfo().noquote() << "Catálogo:" << systems.systems.size() << "sistemas";
        // Cada sistema de la bandeja está en el catálogo con el mismo código: es lo que se vincula al proyecto.
        for (const auto& r : inbox.requirements)
            QVERIFY2(std::any_of(systems.systems.cbegin(), systems.systems.cend(), [&r](const RequirementSystem& sys) { return sys.code == r.systemCode; }),
                     qPrintable(r.systemCode));

        if (inbox.requirements.isEmpty()) return;

        const QString id = inbox.requirements.first().id;
        RequirementDetailResult detail;
        done = false;
        client.fetchDetail(s, id, [&](const RequirementDetailResult& r) { detail = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 60000);
        QVERIFY2(detail.ok, qPrintable(detail.error));
        QCOMPARE(detail.detail.id, id);
        QVERIFY(!detail.detail.state.isEmpty());
        QStringList sections;
        for (const auto& section : detail.detail.sections) sections << QStringLiteral("%1 (%2)").arg(section.title).arg(section.fields.size());
        qInfo().noquote() << "Ficha" << id << "·" << detail.detail.requestType << "·" << detail.detail.systemCode
                          << "·" << detail.detail.fields.size() << "datos ·" << detail.detail.attachments.size() << "adjuntos";
        qInfo().noquote() << "Secciones:" << sections.join(QStringLiteral(", "));
        qInfo().noquote() << "Descripción:" << detail.detail.description.left(300);

        // Un número que no existe es NotFound, no una ficha en blanco.
        RequirementDetailResult missing;
        done = false;
        client.fetchDetail(s, QStringLiteral("1"), [&](const RequirementDetailResult& r) { missing = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 60000);
        QVERIFY(!missing.ok);
        qInfo().noquote() << "Requerimiento 1:" << missing.error;
        QVERIFY2(missing.failure == RequirementSourceFailure::NotFound, qPrintable(missing.error));
    }
};

QTEST_MAIN(GesreqClientTest)
#include "test_gesreq_client.moc"
