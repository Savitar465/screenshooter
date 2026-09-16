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
#include <QTemporaryDir>
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

/// Un campo del envío multipart tal y como lo recibe el servidor.
struct FormPart {
    QString name;
    QString fileName;   // sólo el adjunto
    QByteArray value;
};

QString captured(const QByteArray& head, const char* pattern) {
    const QRegularExpression re(QString::fromLatin1(pattern));
    return re.match(QString::fromUtf8(head)).captured(1);
}

/// Deshace el multipart/form-data del envío, para comprobar qué campos llegaron y con qué valor.
QList<FormPart> multipartParts(const HttpRequest& r) {
    QList<FormPart> parts;
    const QByteArray type = r.header("content-type");
    const int at = type.indexOf("boundary=");
    if (at < 0) return parts;
    // Qt escribe el delimitador entre comillas: `boundary="boundary_.oOo._…"`.
    QByteArray declared = type.mid(at + 9).trimmed();
    if (declared.startsWith('"') && declared.endsWith('"')) declared = declared.mid(1, declared.size() - 2);
    const QByteArray boundary = "--" + declared;
    for (int from = r.body.indexOf(boundary); from >= 0;) {
        const int start = from + boundary.size();
        if (r.body.mid(start, 2) == "--") break;   // el delimitador de cierre
        const int next = r.body.indexOf(boundary, start);
        const QByteArray chunk = r.body.mid(start, (next < 0 ? r.body.size() : next) - start);
        const int headerEnd = chunk.indexOf("\r\n\r\n");
        if (headerEnd < 0) break;
        QByteArray value = chunk.mid(headerEnd + 4);
        while (value.endsWith('\n') || value.endsWith('\r')) value.chop(1);
        parts << FormPart{captured(chunk.left(headerEnd), R"re(name="([^"]*)")re"),
                          captured(chunk.left(headerEnd), R"re(filename="([^"]*)")re"), value};
        from = next;
    }
    return parts;
}

QString fieldValue(const QList<FormPart>& parts, const char* name) {
    for (const auto& part : parts)
        if (part.name == QString::fromLatin1(name)) return QString::fromUtf8(part.value);
    return {};
}

int fieldCount(const QList<FormPart>& parts, const char* name) {
    return int(std::count_if(parts.cbegin(), parts.cend(), [name](const FormPart& p) { return p.name == QString::fromLatin1(name); }));
}

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
    QByteArray gestion = fixture("gestion_calidad.html");
    QByteArray controlForm = fixture("control_calidad_form.html");
    /// Lo que responde el envío del control, con la forma que espera `Anb.form.ajax`.
    QByteArray saveResponse = R"({"state":"OK","data":{"id":"2025101","accion":"calidadreg","estado":"CONTROL DE CALIDAD OBSERVADO","message":"Registro Control Calidad realizado"}})";
    int saveStatus = 200;                 // 500 = el servidor revienta al guardar
    QByteArray saveContentType = "application/json";
    QList<FormPart> saved;   // el último envío recibido, campo a campo
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
        // Pantalla de gestión: sólo ofrece el botón del control si se la pide con el estado con el que
        // el requerimiento figura en la bandeja, como el GESREQ real.
        server.route("GET", "/greq/calidadregGestionRequerimiento.do", [this, login](const HttpRequest& r) {
            if (!signedIn(r)) return page(login);
            if (!query(r).hasQueryItem(QStringLiteral("estado"))) {
                QByteArray withoutButtons = gestion;
                withoutButtons.replace("calidadregFuncionalForm.do", "#");
                return page(withoutButtons);
            }
            return page(gestion);
        });
        // Formulario del control del sistema pedido: lo que cambia entre sistemas es el ítem y su código.
        server.route("GET", "/greq/calidadregFuncionalForm.do", [this, login](const HttpRequest& r) {
            if (!signedIn(r)) return page(login);
            const QUrlQuery q = query(r);
            QByteArray form = controlForm;
            form.replace("name=\"id\" value=\"1\"", "name=\"id\" value=\"" + q.queryItemValue(QStringLiteral("idItem")).toUtf8() + "\"");
            form.replace("name=\"sis_cod\" value=\"PORTAL WEB\"",
                         "name=\"sis_cod\" value=\"" + q.queryItemValue(QStringLiteral("sis_cod"), QUrl::FullyDecoded).toUtf8() + "\"");
            return page(form);
        });
        server.route("POST", "/greq/calidadregGestionControlGuardar.do", [this, login](const HttpRequest& r) {
            if (!signedIn(r)) return page(login);
            saved = multipartParts(r);
            return HttpResponse{saveStatus, saveResponse, saveContentType, {}};
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
    /// El envío llevaba el adjunto, con el nombre del fichero.
    FormPart attachment() const {
        for (const auto& part : saved)
            if (part.name == QStringLiteral("arch_funcional")) return part;
        return {};
    }

private:
    bool signedIn(const HttpRequest& r) const { return keepsSessions && authenticated.contains(sessionCookie(r)); }
    static QUrlQuery query(const HttpRequest& r) { return QUrlQuery(QUrl(QString::fromUtf8(r.path)).query()); }
    int m_opened = 0;
};
/// Un acta cualquiera en disco: lo que GESREQ exige es el adjunto, no su contenido.
QString writeRecord(const QTemporaryDir& dir, const QString& name = QStringLiteral("ActaR213.docx")) {
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) qFatal("No se pudo escribir el acta de prueba");
    file.write("PK\x03\x04 acta de control de calidad");
    return path;
}

/// El registro de una ronda observada: dos fallos de funcionamiento, uno de forma y tres recomendaciones.
RequirementRegistration observedRegistration(const QString& document) {
    RequirementRegistration registration;
    registration.requirementId = QStringLiteral("2025101");
    registration.systemCode = QStringLiteral("PORTAL WEB");
    registration.result = QStringLiteral("Observado");
    registration.comment = QStringLiteral("Quedan 2 observaciones de funcionamiento");
    registration.attachmentPath = document;
    registration.observations = {{QStringLiteral("A"), 2, 0}, {QStringLiteral("B"), 0, 0}, {QStringLiteral("C"), 1, 0},
                                 {QStringLiteral("D"), 3, 0}, {QStringLiteral("E"), 0, 0}};
    return registration;
}
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

    // ---- Registro del control de calidad --------------------------------------------------------
    void registersTheControlWithTheRecordAttached() {
        FakeGesreq gesreq;
        QTemporaryDir dir;
        GesreqClient client;
        RequirementRegistrationResult out;
        bool done = false;
        client.registerResult(gesreq.settings(), observedRegistration(writeRecord(dir)),
                              [&](const RequirementRegistrationResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QVERIFY(!out.uncertain);

        // La pantalla de gestión se pide con el estado que da la bandeja: sin él no trae el formulario.
        QVERIFY(std::any_of(gesreq.server.requests.cbegin(), gesreq.server.requests.cend(), [](const HttpRequest& r) {
            return r.method == "GET" && r.path.startsWith("/greq/calidadregGestionRequerimiento.do") && r.path.contains("estado=");
        }));

        // Lo que decide QAflow: el resultado, su comentario y el resumen de observaciones del acta.
        QCOMPARE(fieldValue(gesreq.saved, "resultado_control"), QStringLiteral("OBSERVADO"));
        QCOMPARE(fieldValue(gesreq.saved, "obs_controlfuncional"), QStringLiteral("Quedan 2 observaciones de funcionamiento"));
        QCOMPARE(fieldValue(gesreq.saved, "tot_obs_func"), QStringLiteral("2"));
        QCOMPARE(fieldValue(gesreq.saved, "tot_obs_datos"), QStringLiteral("0"));
        QCOMPARE(fieldValue(gesreq.saved, "tot_obs_forma"), QStringLiteral("1"));
        QCOMPARE(fieldValue(gesreq.saved, "tot_obs_rec"), QStringLiteral("3"));
        QCOMPARE(fieldValue(gesreq.saved, "tot_obs_vul"), QStringLiteral("0"));
        // Lo demás, tal y como lo puso el servidor en su formulario.
        QCOMPARE(fieldValue(gesreq.saved, "gestion"), QStringLiteral("2025"));
        QCOMPARE(fieldValue(gesreq.saved, "corr"), QStringLiteral("101"));
        QCOMPARE(fieldValue(gesreq.saved, "tip_control"), QStringLiteral("CALIDAD"));
        QCOMPARE(fieldValue(gesreq.saved, "cod_asignado"), QStringLiteral("15573"));
        QCOMPARE(fieldValue(gesreq.saved, "fecha_ini"), QStringLiteral("11/08/2025"));
        // Las correcciones son las que lleva el requerimiento y viajan una sola vez: la casilla que la
        // ventana deshabilita no se envía, sólo el oculto con el valor del sistema.
        QCOMPARE(fieldCount(gesreq.saved, "tot_corr_func"), 1);
        QCOMPARE(fieldValue(gesreq.saved, "tot_corr_func"), QStringLiteral("36"));
        // Los campos deshabilitados no se envían.
        QCOMPARE(fieldCount(gesreq.saved, "observaciones"), 0);
        QCOMPARE(fieldCount(gesreq.saved, "obs_controlcalidad"), 0);
        // Y el acta va en el campo del adjunto, con su nombre.
        QCOMPARE(gesreq.attachment().fileName, QStringLiteral("ActaR213.docx"));
        QVERIFY(!gesreq.attachment().value.isEmpty());
    }

    void registersTheControlOfTheSystemUnderTest() {
        FakeGesreq gesreq;
        QTemporaryDir dir;
        RequirementRegistration registration = observedRegistration(writeRecord(dir));
        registration.systemCode = QStringLiteral("PORTAL PAGOS");   // el requerimiento toca dos sistemas
        GesreqClient client;
        RequirementRegistrationResult out;
        bool done = false;
        client.registerResult(gesreq.settings(), registration, [&](const RequirementRegistrationResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(fieldValue(gesreq.saved, "id"), QStringLiteral("2"));
        QCOMPARE(fieldValue(gesreq.saved, "sis_cod"), QStringLiteral("PORTAL PAGOS"));
    }

    void aSystemWithoutControlIsNotRegisteredInAnother() {
        FakeGesreq gesreq;
        QTemporaryDir dir;
        RequirementRegistration registration = observedRegistration(writeRecord(dir));
        registration.systemCode = QStringLiteral("OTRO SISTEMA");
        GesreqClient client;
        RequirementRegistrationResult out;
        bool done = false;
        client.registerResult(gesreq.settings(), registration, [&](const RequirementRegistrationResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.failure == RequirementSourceFailure::NotFound);
        QVERIFY2(out.error.contains(QStringLiteral("PORTAL WEB")), qPrintable(out.error));
        QVERIFY(gesreq.saved.isEmpty());
    }

    void aConformeControlIsSentAsOk() {
        FakeGesreq gesreq;
        QTemporaryDir dir;
        RequirementRegistration registration = observedRegistration(writeRecord(dir));
        registration.result = QStringLiteral("Conforme");
        registration.comment = QStringLiteral("Sin observaciones");
        // Conforme con recomendaciones sí, que no impiden dar por bueno el requerimiento.
        registration.observations = {{QStringLiteral("A"), 0, 2}, {QStringLiteral("B"), 0, 0}, {QStringLiteral("C"), 0, 1},
                                     {QStringLiteral("D"), 1, 0}, {QStringLiteral("E"), 0, 0}};
        GesreqClient client;
        RequirementRegistrationResult out;
        bool done = false;
        client.registerResult(gesreq.settings(), registration, [&](const RequirementRegistrationResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY2(out.ok, qPrintable(out.error));
        QCOMPARE(fieldValue(gesreq.saved, "resultado_control"), QStringLiteral("OK"));
        QCOMPARE(fieldValue(gesreq.saved, "tot_obs_rec"), QStringLiteral("1"));
    }

    void registeringWithoutTheRecordIsRefusedBeforeAsking() {
        FakeGesreq gesreq;
        RequirementRegistration registration = observedRegistration(QString());
        GesreqClient client;
        RequirementRegistrationResult out;
        bool done = false;
        client.registerResult(gesreq.settings(), registration, [&](const RequirementRegistrationResult& r) { out = r; done = true; });
        QVERIFY(done);
        QVERIFY(out.failure == RequirementSourceFailure::Configuration);
        QVERIFY2(out.error.contains(QStringLiteral("acta")), qPrintable(out.error));
        QVERIFY(gesreq.server.requests.isEmpty());   // no se toca el sistema para algo que va a rechazar
    }

    // Las mismas reglas se pueden preguntar sin tocar la red: así la pantalla avisa antes de publicar.
    void theRulesOfTheFormCanBeAskedBeforeSending() {
        QTemporaryDir dir;
        GesreqClient client;
        const RequirementRegistration good = observedRegistration(writeRecord(dir));
        QVERIFY(client.registrationProblem(good).isEmpty());

        QVERIFY(client.registrationProblem(observedRegistration(QString())).contains(QStringLiteral("acta")));

        RequirementRegistration conforme = good;
        conforme.result = QStringLiteral("Conforme");
        QVERIFY(client.registrationProblem(conforme).contains(QStringLiteral("OK")));

        RequirementRegistration pending = good;
        pending.result = QStringLiteral("Pendiente");
        QVERIFY(!client.registrationProblem(pending).isEmpty());

        RequirementRegistration onlyRecommendations = good;
        onlyRecommendations.observations = {{QStringLiteral("A"), 0, 0}, {QStringLiteral("B"), 0, 0}, {QStringLiteral("C"), 0, 0},
                                            {QStringLiteral("D"), 2, 0}, {QStringLiteral("E"), 0, 0}};
        QVERIFY(!client.registrationProblem(onlyRecommendations).isEmpty());
    }

    void aResultThatContradictsTheObservationsIsRefused() {
        FakeGesreq gesreq;
        QTemporaryDir dir;
        GesreqClient client;
        // «OK» con observaciones que no son recomendaciones: GESREQ no lo admite.
        RequirementRegistration conforme = observedRegistration(writeRecord(dir));
        conforme.result = QStringLiteral("Conforme");
        RequirementRegistrationResult out;
        bool done = false;
        client.registerResult(gesreq.settings(), conforme, [&](const RequirementRegistrationResult& r) { out = r; done = true; });
        QVERIFY(done);
        QVERIFY(out.failure == RequirementSourceFailure::Configuration);
        QVERIFY2(out.error.contains(QStringLiteral("OK")), qPrintable(out.error));

        // Y «OBSERVADO» sin ninguna observación fuera de las recomendaciones, tampoco.
        RequirementRegistration observado = observedRegistration(writeRecord(dir));
        observado.observations = {{QStringLiteral("A"), 0, 0}, {QStringLiteral("B"), 0, 0}, {QStringLiteral("C"), 0, 0},
                                  {QStringLiteral("D"), 4, 0}, {QStringLiteral("E"), 0, 0}};
        done = false;
        client.registerResult(gesreq.settings(), observado, [&](const RequirementRegistrationResult& r) { out = r; done = true; });
        QVERIFY(done);
        QVERIFY(out.failure == RequirementSourceFailure::Configuration);
        QVERIFY(gesreq.server.requests.isEmpty());
    }

    // Un 500 no es «no hubo respuesta»: la página del servidor dice qué reventó y eso es lo que se enseña.
    void aServerErrorIsToldWithWhatItsPageSays() {
        FakeGesreq gesreq;
        gesreq.saveStatus = 500;
        gesreq.saveContentType = "text/html";
        gesreq.saveResponse =
                "<html><head><title>HTTP Status 500 – Internal Server Error</title></head><body>"
                "<h1>HTTP Status 500 – Internal Server Error</h1>"
                "<p><b>Message</b> Request processing failed</p>"
                "<p><b>Exception</b></p>"
                "<pre>java.sql.SQLException: ORA-12899: value too large for column OBS_CONTROLFUNCIONAL\n"
                "\tat oracle.jdbc.driver.T4CTTIoer.processError(T4CTTIoer.java:450)</pre></body></html>";
        QTemporaryDir dir;
        GesreqClient client;
        RequirementRegistrationResult out;
        bool done = false;
        client.registerResult(gesreq.settings(), observedRegistration(writeRecord(dir)),
                              [&](const RequirementRegistrationResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY2(out.error.contains(QStringLiteral("500")), qPrintable(out.error));
        QVERIFY2(out.error.contains(QStringLiteral("ORA-12899")), qPrintable(out.error));
        QVERIFY2(out.error.contains(QStringLiteral("Request processing failed")), qPrintable(out.error));
        // Pudo guardarlo antes de fallar: hay que comprobarlo en el sistema antes de repetirlo.
        QVERIFY(out.uncertain);
    }

    // El envío se escribe como el de un navegador: hay servidores (el de GESREQ) que con el multipart
    // que compone Qt —delimitador entre comillas y cada campo con su Content-Type— responden un 500.
    void theRequestIsShapedLikeABrowserForm() {
        FakeGesreq gesreq;
        QTemporaryDir dir;
        GesreqClient client;
        bool done = false;
        client.registerResult(gesreq.settings(), observedRegistration(writeRecord(dir)),
                              [&](const RequirementRegistrationResult& r) { done = r.ok; });
        QTRY_VERIFY(done);
        const auto save = std::find_if(gesreq.server.requests.cbegin(), gesreq.server.requests.cend(), [](const HttpRequest& r) {
            return r.method == "POST" && r.path.endsWith("calidadregGestionControlGuardar.do");
        });
        QVERIFY(save != gesreq.server.requests.cend());
        const QByteArray type = save->header("content-type");
        QVERIFY2(type.startsWith("multipart/form-data; boundary="), type.constData());
        QVERIFY2(!type.contains('"'), type.constData());                    // el delimitador, sin comillas
        QVERIFY(!save->body.contains("Content-Type: text/plain"));          // los campos, sin Content-Type
        QVERIFY(save->body.contains("Content-Disposition: form-data; name=\"resultado_control\"\r\n\r\n"));
        // El acta va donde el formulario pone su campo, no al final: antes de las observaciones.
        const int file = save->body.indexOf("name=\"arch_funcional\"");
        const int comment = save->body.indexOf("name=\"obs_controlfuncional\"");
        const int result = save->body.indexOf("name=\"resultado_control\"");
        QVERIFY(file > 0 && result > 0 && comment > 0);
        QVERIFY(result < file && file < comment);
        QVERIFY(save->body.endsWith("--\r\n"));
    }

    // El comentario viaja recortado a lo que admite el campo del formulario.
    void aLongCommentIsTrimmedToWhatTheFormTakes() {
        FakeGesreq gesreq;
        QTemporaryDir dir;
        RequirementRegistration registration = observedRegistration(writeRecord(dir));
        registration.comment = QString(gesreq::kControlCommentMax + 500, QLatin1Char('x'));
        GesreqClient client;
        bool done = false;
        client.registerResult(gesreq.settings(), registration, [&](const RequirementRegistrationResult& r) { done = r.ok; });
        QTRY_VERIFY(done);
        const QString sent = fieldValue(gesreq.saved, "obs_controlfuncional");
        QCOMPARE(sent.size(), gesreq::kControlCommentMax);
        QVERIFY(sent.endsWith(QChar(0x2026)));
    }

    void whatGesreqRejectsIsToldWithItsReason() {
        FakeGesreq gesreq;
        gesreq.saveResponse = R"({"state":"ERROR","data":{"message":"Se debe adjuntar el archivo de Control Realizado"}})";
        QTemporaryDir dir;
        GesreqClient client;
        RequirementRegistrationResult out;
        bool done = false;
        client.registerResult(gesreq.settings(), observedRegistration(writeRecord(dir)),
                              [&](const RequirementRegistrationResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.failure == RequirementSourceFailure::Rejected);
        QVERIFY(!out.retryable());
        QVERIFY(!out.uncertain);
        QVERIFY2(out.error.contains(QStringLiteral("adjuntar")), qPrintable(out.error));
    }

    void aRequirementOutOfTheInboxIsNotRegistered() {
        FakeGesreq gesreq;
        QTemporaryDir dir;
        RequirementRegistration registration = observedRegistration(writeRecord(dir));
        registration.requirementId = QStringLiteral("2029999");
        GesreqClient client;
        RequirementRegistrationResult out;
        bool done = false;
        client.registerResult(gesreq.settings(), registration, [&](const RequirementRegistrationResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(out.failure == RequirementSourceFailure::NotFound);
        QVERIFY2(out.error.contains(QStringLiteral("bandeja")), qPrintable(out.error));
        QVERIFY(gesreq.saved.isEmpty());
    }

    void aCutSendIsMarkedUncertain() {
        FakeGesreq gesreq;
        // El envío sale y la respuesta se pierde: pudo quedar registrado, así que no se repite sin mirar.
        gesreq.server.route("POST", "/greq/calidadregGestionControlGuardar.do",
                            [](const HttpRequest&) { return HttpResponse{500, QByteArray(), "text/html", {}}; });
        QTemporaryDir dir;
        GesreqClient client;
        RequirementRegistrationResult out;
        bool done = false;
        client.registerResult(gesreq.settings(), observedRegistration(writeRecord(dir)),
                              [&](const RequirementRegistrationResult& r) { out = r; done = true; });
        QTRY_VERIFY(done);
        QVERIFY(!out.ok);
        QVERIFY(out.uncertain);
        QVERIFY(out.retryable());
        QCOMPARE(gesreq.count("POST", "/greq/calidadregGestionControlGuardar.do"), 1);   // no se reintenta solo
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
