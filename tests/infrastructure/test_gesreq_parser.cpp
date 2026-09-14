// Extractor de GESREQ (infrastructure/requirements/GesreqParser) sobre copias anonimizadas de sus
// páginas (tests/fixtures/gesreq): la bandeja de control de calidad, la ficha de un requerimiento, la
// ficha vacía que el sistema devuelve sin sesión y el formulario de login. Si GESREQ cambia su marcado,
// se actualizan los fixtures con la página nueva (sin datos reales) y se ajusta el extractor.

#include "infrastructure/requirements/GesreqParser.h"

#include <QFile>
#include <QRegularExpression>
#include <QtTest>

#include <algorithm>

using namespace qaflow;

namespace {
QString fixture(const char* name) {
    QFile f(QString::fromUtf8(QAFLOW_FIXTURES_DIR "/gesreq/") + QString::fromLatin1(name));
    if (!f.open(QIODevice::ReadOnly)) qFatal("No se encuentra el fixture %s", name);
    return QString::fromUtf8(f.readAll());
}

RequirementSourceSettings settings() {
    RequirementSourceSettings s;
    s.url = QStringLiteral("http://gesreq.test:7401/greq/");
    s.user = QStringLiteral("QAUSR0101");
    s.password = QStringLiteral("s3creta");
    return s;
}

gesreq::DetailPage detail() { return gesreq::parseDetail(fixture("detalle.html"), QStringLiteral("2025101"), settings()); }
} // namespace

class GesreqParserTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Bandeja de control de calidad ---------------------------------------------------------
    void readsEveryRequirementOfTheInbox() {
        const gesreq::InboxPage page = gesreq::parseInbox(fixture("bandeja_calidad.html"), settings());
        QVERIFY2(page.ok, qPrintable(page.error));
        QCOMPARE(page.requirements.size(), 3);
        const ExternalRequirement& r = page.requirements[0];
        QCOMPARE(r.id, QStringLiteral("2025101"));
        QCOMPARE(r.system, QStringLiteral("PORTAL WEB-PORTAL INSTITUCIONAL"));
        QCOMPARE(r.systemCode, QStringLiteral("PORTAL WEB"));
        QCOMPARE(r.systemName, QStringLiteral("PORTAL INSTITUCIONAL"));
        QCOMPARE(r.summary, QStringLiteral("Nuevo formulario de contacto con validación de correo"));
        QCOMPARE(r.requestedOn, QDate(2025, 5, 15));
        QCOMPARE(r.assignedFrom, QDate(2025, 8, 11));
        QCOMPARE(r.assignedUntil, QDate(2026, 1, 27));
        QCOMPARE(r.requestingUnit, QStringLiteral("GNTI"));
        QCOMPARE(r.requester, QStringLiteral("PÉREZ GÓMEZ ANA"));
        QCOMPARE(r.user, QStringLiteral("LÓPEZ RUIZ CARLOS"));
        QCOMPARE(r.priority, QStringLiteral("ALTA"));
        QCOMPARE(r.detailUrl, QStringLiteral("http://gesreq.test:7401/greq/publico.do?id=2025101&bandera=1"));
        QCOMPARE(page.requirements[2].id, QStringLiteral("2026103"));
    }

    // Cada estado es una etiqueta propia en la celda: leída como texto quedaría uno solo pegado.
    void keepsEveryStateOfARequirement() {
        const gesreq::InboxPage page = gesreq::parseInbox(fixture("bandeja_calidad.html"), settings());
        QVERIFY2(page.ok, qPrintable(page.error));
        QCOMPARE(page.requirements[0].states, QStringList({QStringLiteral("CONTROL DE CALIDAD OBSERVADO"), QStringLiteral("CONTROL FUNCIONAL")}));
        QCOMPARE(page.requirements[1].states, QStringList({QStringLiteral("CONTROL CALIDAD ASIGNADO")}));
    }

    void theSystemCodeIsWhatComesBeforeTheFirstDash() {
        const gesreq::InboxPage page = gesreq::parseInbox(fixture("bandeja_calidad.html"), settings());
        QVERIFY2(page.ok, qPrintable(page.error));
        QCOMPARE(page.requirements[1].systemCode, QStringLiteral("INVENTARIO"));
        QCOMPARE(page.requirements[1].systemName, QStringLiteral("SISTEMA DE INVENTARIO - MODULO DE INGRESO"));
        QCOMPARE(splitSystem(QStringLiteral("SUMA2SALIDA")), qMakePair(QStringLiteral("SUMA2SALIDA"), QString()));
    }

    void cellsResolveHtmlEntities() {
        const gesreq::InboxPage page = gesreq::parseInbox(fixture("bandeja_calidad.html"), settings());
        QVERIFY2(page.ok, qPrintable(page.error));
        QCOMPARE(page.requirements[1].summary, QStringLiteral("Ajustes de impresión & reportes"));
    }

    void anInboxWithoutRequirementsIsAnEmptyListNotAnError() {
        QString html = fixture("bandeja_calidad.html");
        html.replace(QRegularExpression(QStringLiteral("<tbody>.*</tbody>"), QRegularExpression::DotMatchesEverythingOption),
                     QStringLiteral("<tbody>\n            </tbody>"));
        const gesreq::InboxPage page = gesreq::parseInbox(html, settings());
        QVERIFY2(page.ok, qPrintable(page.error));
        QVERIFY(page.requirements.isEmpty());
    }

    // El caso que no puede pasar: la página de login leída como una bandeja vacía.
    void theLoginPageIsNotAnEmptyInbox() {
        const QString login = fixture("login.html");
        QVERIFY(gesreq::isLoginPage(login));
        QVERIFY(!gesreq::isLoginPage(fixture("bandeja_calidad.html")));
        const gesreq::InboxPage page = gesreq::parseInbox(login, settings());
        QVERIFY(!page.ok);
        QVERIFY(!page.error.isEmpty());
    }

    void aMissingColumnIsReportedByItsName() {
        QString html = fixture("bandeja_calidad.html");
        html.replace(QStringLiteral("<th>Estado</th>"), QStringLiteral("<th>Situación</th>"));
        const gesreq::InboxPage page = gesreq::parseInbox(html, settings());
        QVERIFY(!page.ok);
        QVERIFY2(page.error.contains(QStringLiteral("Estado")), qPrintable(page.error));
    }

    // Las columnas se buscan por su cabecera: si el sistema quita o mueve una, el resto se sigue leyendo bien.
    void columnsAreFoundByTheirHeaderNotTheirPosition() {
        QString html = fixture("bandeja_calidad.html");
        html.remove(QStringLiteral("<th class=\"oculto\">Orden</th>"));
        html.remove(QStringLiteral("<td class=\"oculto\">1</td>"));
        const gesreq::InboxPage page = gesreq::parseInbox(html, settings());
        QVERIFY2(page.ok, qPrintable(page.error));
        QCOMPARE(page.requirements.size(), 3);
        QCOMPARE(page.requirements[0].systemCode, QStringLiteral("PORTAL WEB"));
        QCOMPARE(page.requirements[0].priority, QStringLiteral("ALTA"));
    }

    void aRowWithoutARequirementNumberIsAnError() {
        QString html = fixture("bandeja_calidad.html");
        html.replace(QStringLiteral("class=\"btn btn-default\">2026103</a>"), QStringLiteral("class=\"btn btn-default\">Ver</a>"));
        const gesreq::InboxPage page = gesreq::parseInbox(html, settings());
        QVERIFY(!page.ok);
        QVERIFY2(page.error.contains(QStringLiteral("Ver")), qPrintable(page.error));
    }

    // ---- Sesión --------------------------------------------------------------------------------
    void recognisesTheSignedInPageAndItsUser() {
        const QString inbox = fixture("bandeja_calidad.html");
        QVERIFY(gesreq::isAuthenticatedPage(inbox));
        QVERIFY(!gesreq::isAuthenticatedPage(fixture("login.html")));
        QCOMPARE(gesreq::userName(inbox), QStringLiteral("Tester Uno, Ana"));
    }

    // Las plantillas de avisos de la página ({message}) no son el motivo de un login rechazado.
    void messageTemplatesAreNotALoginMessage() {
        QVERIFY(gesreq::loginMessage(fixture("login.html")).isEmpty());
        QString rejected = fixture("login.html");
        rejected.replace(QStringLiteral("$(document).ready(function() {"),
                         QStringLiteral("$(document).ready(function() {\n Anb.message.notification('Usuario o clave incorrectos');"));
        QCOMPARE(gesreq::loginMessage(rejected), QStringLiteral("Usuario o clave incorrectos"));
    }

    // ---- Ficha del requerimiento ---------------------------------------------------------------
    void readsTheGeneralDataOfTheRequirement() {
        const gesreq::DetailPage page = detail();
        QVERIFY2(page.kind == gesreq::DetailPage::Kind::Detail, qPrintable(page.error));
        const RequirementDetail& d = page.detail;
        QCOMPARE(d.id, QStringLiteral("2025101"));
        QCOMPARE(d.requestType, QStringLiteral("NUEVA FUNCIONALIDAD"));
        QCOMPARE(d.reference, QStringLiteral("Formulario de contacto del portal con validación de correo"));
        QCOMPARE(d.requestingUnit, QStringLiteral("GERENCIA DE TECNOLOGÍA / DEPARTAMENTO DE DESARROLLO"));
        QCOMPARE(d.requester, QStringLiteral("PÉREZ GÓMEZ ANA"));
        QCOMPARE(d.priority, QStringLiteral("ALTA"));
        QCOMPARE(d.state, QStringLiteral("CONTROL DE CALIDAD OBSERVADO"));
        QCOMPARE(d.systemCode, QStringLiteral("PORTAL WEB"));
        QCOMPARE(d.url, QStringLiteral("http://gesreq.test:7401/greq/publico.do?id=2025101&bandera=1"));
        QCOMPARE(d.field(QStringLiteral("Fecha solicitado(S), observado(O), aceptado(A)")), QStringLiteral("S: 14/03/2025, A: 14/03/2025"));
        QCOMPARE(d.field(QStringLiteral("Archivo de respaldo final")), QString());
    }

    void theDescriptionKeepsItsParagraphsAndBullets() {
        const gesreq::DetailPage page = detail();
        QVERIFY2(page.kind == gesreq::DetailPage::Kind::Detail, qPrintable(page.error));
        QCOMPARE(page.detail.description, QStringLiteral("El formulario de contacto debe:\n"
                                                         "• Validar el correo.\n"
                                                         "• Enviar una copia al solicitante.\n"
                                                         "Ambiente: calidad."));
    }

    // El historial de cada control va en ventanas modales dentro de la ficha y repite etiquetas de los
    // datos vigentes con valores de rondas anteriores: no se mezcla con ellos.
    void readsTheSectionsInOrderWithoutTheHistory() {
        const gesreq::DetailPage page = detail();
        QVERIFY2(page.kind == gesreq::DetailPage::Kind::Detail, qPrintable(page.error));
        const RequirementDetail& d = page.detail;
        QStringList titles;
        for (const auto& s : d.sections) titles << s.title;
        QCOMPARE(titles, QStringList({QStringLiteral("Datos Asignado"), QStringLiteral("Datos Control de Calidad Solicitado"),
                                      QStringLiteral("Datos Control de Calidad Asignado"), QStringLiteral("Datos Control Funcional")}));
        QCOMPARE(d.field(QStringLiteral("Url")), QStringLiteral("http://qa.example.test/portal"));
        QCOMPARE(d.field(QStringLiteral("Usuario aplicación")), QStringLiteral("tester"));
        QCOMPARE(d.field(QStringLiteral("Recurso(s)")), QStringLiteral("LÓPEZ RUIZ CARLOS"));
        QCOMPARE(d.sections[2].fields.size(), 3);   // recurso, fechas y archivo; nada del historial
        for (const auto& s : d.sections)
            for (const auto& f : s.fields) QVERIFY2(!f.value.contains(QStringLiteral("HISTORIAL")), qPrintable(f.label));
    }

    void collectsTheAttachmentsWithWhatTheyAre() {
        const gesreq::DetailPage page = detail();
        QVERIFY2(page.kind == gesreq::DetailPage::Kind::Detail, qPrintable(page.error));
        const QList<RequirementAttachment>& files = page.detail.attachments;
        QCOMPARE(files.size(), 2);   // la del historial se queda fuera
        QCOMPARE(files[0].label, QStringLiteral("Archivo de respaldo inicial"));
        QCOMPARE(files[0].fileName, QStringLiteral("2025101REQ20250314183739.pdf"));
        QCOMPARE(files[0].url, QStringLiteral("http://gesreq.test:7401/greq/docDownload.do?doc=2025/2025101REQ20250314183739.pdf"));
        QCOMPARE(files[1].label, QStringLiteral("Datos Control de Calidad Asignado · Archivo"));
        QCOMPARE(files[1].fileName, QStringLiteral("CONTROLCALIDAD202510120260608104411.docx"));
        QCOMPARE(page.detail.field(QStringLiteral("Archivo de respaldo inicial")), QStringLiteral("2025101REQ20250314183739.pdf"));
    }

    // Lo que GESREQ devuelve con HTTP 200 sin sesión: no es un requerimiento en blanco.
    void theEmptyDetailIsNotARequirement() {
        const gesreq::DetailPage page = gesreq::parseDetail(fixture("detalle_vacio.html"), QStringLiteral("2025101"), settings());
        QVERIFY(page.kind == gesreq::DetailPage::Kind::Empty);
        QVERIFY(gesreq::parseDetail(fixture("login.html"), QStringLiteral("2025101"), settings()).kind == gesreq::DetailPage::Kind::Unexpected);
    }

    // Con sesión, un número que no existe trae la ficha con ese número y sin datos: no es un cambio de
    // estructura ni una sesión caducada.
    void aRequirementThatDoesNotExistIsMissing() {
        const gesreq::DetailPage page = gesreq::parseDetail(fixture("detalle_inexistente.html"), QStringLiteral("2025999"), settings());
        QVERIFY2(page.kind == gesreq::DetailPage::Kind::Missing, qPrintable(page.error));
    }

    void theDetailOfAnotherRequirementIsUnexpected() {
        const gesreq::DetailPage page = gesreq::parseDetail(fixture("detalle.html"), QStringLiteral("2025999"), settings());
        QVERIFY(page.kind == gesreq::DetailPage::Kind::Unexpected);
        QVERIFY2(page.error.contains(QStringLiteral("2025101")) && page.error.contains(QStringLiteral("2025999")), qPrintable(page.error));
    }

    void aDetailWithoutItsStateIsUnexpected() {
        QString html = fixture("detalle.html");
        html.replace(QStringLiteral("<th>Estado:</th>\n                    <td class=\"observado\">"), QStringLiteral("<th>Situación:</th>\n                    <td class=\"observado\">"));
        const gesreq::DetailPage page = gesreq::parseDetail(html, QStringLiteral("2025101"), settings());
        QVERIFY(page.kind == gesreq::DetailPage::Kind::Unexpected);
        QVERIFY2(page.error.contains(QStringLiteral("Estado")), qPrintable(page.error));
    }

    // ---- Catálogo de sistemas ------------------------------------------------------------------
    void readsTheCatalogOfSystemsWithTheCodeUsedInTheInbox() {
        const gesreq::SystemsPage page = gesreq::parseSystems(fixture("sistemas.html"));
        QVERIFY2(page.ok, qPrintable(page.error));
        QCOMPARE(page.systems.size(), 5);   // sin la opción en blanco, la comentada ni los otros desplegables
        QCOMPARE(page.systems[0].code, QStringLiteral("FACTURACION"));
        QCOMPARE(page.systems[0].name, QStringLiteral("FACTURACION ELECTRONICA"));
        QCOMPARE(page.systems[1].name, QStringLiteral("SISTEMA DE INVENTARIO - MODULO DE INGRESO"));   // el nombre lleva sus guiones
        QCOMPARE(page.systems[2].code, QStringLiteral("PORTAL WEB"));                                  // y el código, espacios
        QCOMPARE(page.systems[3].name, QStringLiteral("SISTEMA DE GESTIÓN DOCUMENTAL"));
        QCOMPARE(page.systems[4].code, QStringLiteral("ws_ventas"));
        // El código del catálogo es el mismo con el que aparece el sistema en la bandeja.
        const gesreq::InboxPage inbox = gesreq::parseInbox(fixture("bandeja_calidad.html"), settings());
        QCOMPARE(inbox.requirements[0].systemCode, page.systems[2].code);
    }

    void aPageWithoutTheCatalogIsAnError() {
        const gesreq::SystemsPage page = gesreq::parseSystems(fixture("bandeja_calidad.html"));
        QVERIFY(!page.ok);
        QVERIFY(page.systems.isEmpty());
        QVERIFY(!page.error.isEmpty());
    }

    // ---- Texto y fechas ------------------------------------------------------------------------
    void plainTextResolvesEntitiesAndLineBreaks() {
        QCOMPARE(gesreq::toPlainText(QStringLiteral("Usuario aplicaci&oacute;n<br/>l&iacute;nea&nbsp;dos &#8211; &#x41;")),
                 QStringLiteral("Usuario aplicación\nlínea dos – A"));
        // Una entidad desconocida se deja tal cual (los enlaces llevan "&bandera=1").
        QCOMPARE(gesreq::toPlainText(QStringLiteral("publico.do?id=1&bandera=1")), QStringLiteral("publico.do?id=1&bandera=1"));
        QCOMPARE(gesreq::toPlainText(QStringLiteral("<script>var a = '<b>no</b>';</script><b>sí</b>")), QStringLiteral("sí"));
    }

    void datesAreDayMonthYear() {
        QCOMPARE(gesreq::parseDate(QStringLiteral("09/05/2025")), QDate(2025, 5, 9));
        QCOMPARE(gesreq::parseDate(QStringLiteral(" 9/5/2025 ")), QDate(2025, 5, 9));
        QVERIFY(!gesreq::parseDate(QStringLiteral("2025-05-09")).isValid());
        QVERIFY(!gesreq::parseDate(QString()).isValid());
    }
};

QTEST_MAIN(GesreqParserTest)
#include "test_gesreq_parser.moc"
