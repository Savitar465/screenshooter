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

    void aDetailWithoutItsStateIsUnexpected_data() {
        QTest::addColumn<QString>("newline");
        QTest::newRow("LF") << QStringLiteral("\n");
        QTest::newRow("CRLF") << QStringLiteral("\r\n");
    }

    void aDetailWithoutItsStateIsUnexpected() {
        QFETCH(QString, newline);
        QString html = fixture("detalle.html");
        html.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        html.replace(QStringLiteral("\n"), newline);
        const QString stateHeader = QStringLiteral("<th>Estado:</th>");
        QVERIFY(html.contains(stateHeader));
        html.replace(stateHeader, QStringLiteral("<th>Situación:</th>"));
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

    // ---- Registro del control de calidad --------------------------------------------------------
    void theInboxGivesTheLinkThatRegistersEachRequirement() {
        const QString inbox = fixture("bandeja_calidad.html");
        const QString path = gesreq::registrationPath(inbox, QStringLiteral("2025101"));
        // Con el estado con el que figura en la bandeja: sin él la pantalla se abre sin el formulario.
        QVERIFY2(path.contains(QStringLiteral("estado=CONTROL DE CALIDAD OBSERVADO")), qPrintable(path));
        QVERIFY(path.startsWith(QStringLiteral("calidadregGestionRequerimiento.do?id=2025101")));
        QCOMPARE(gesreq::registrationPath(inbox, QStringLiteral("2026103")).contains(QStringLiteral("CORREGIDO")), true);
        QVERIFY(gesreq::registrationPath(inbox, QStringLiteral("2029999")).isEmpty());
    }

    void eachSystemOfTheRequirementHasItsOwnControl() {
        const QList<gesreq::ControlItem> items = gesreq::parseControlItems(fixture("gestion_calidad.html"));
        QCOMPARE(items.size(), 2);
        QCOMPARE(items[0].systemCode, QStringLiteral("PORTAL WEB"));
        QCOMPARE(items[0].systemName, QStringLiteral("PORTAL INSTITUCIONAL"));
        QVERIFY(items[0].formPath.contains(QStringLiteral("idItem=1")));
        QCOMPARE(items[1].systemCode, QStringLiteral("PORTAL PAGOS"));
        QVERIFY(items[1].formPath.contains(QStringLiteral("idItem=2")));
        // Una pantalla sin la tabla de sistemas no ofrece ningún control.
        QVERIFY(gesreq::parseControlItems(fixture("bandeja_calidad.html")).isEmpty());
    }

    void theControlFormIsReadAsTheBrowserWouldSendIt() {
        const gesreq::ControlForm form = gesreq::parseControlForm(fixture("control_calidad_form.html"));
        QVERIFY2(form.ok, qPrintable(form.error));
        auto value = [&form](const QString& name) {
            for (const auto& [field, v] : form.fields)
                if (field == name) return v;
            return QString();
        };
        auto count = [&form](const QString& name) {
            return int(std::count_if(form.fields.cbegin(), form.fields.cend(),
                                     [&name](const QPair<QString, QString>& f) { return f.first == name; }));
        };
        QCOMPARE(value(QStringLiteral("gestion")), QStringLiteral("2025"));
        QCOMPARE(value(QStringLiteral("corr")), QStringLiteral("101"));
        QCOMPARE(value(QStringLiteral("tip_control")), QStringLiteral("CALIDAD"));
        QCOMPARE(value(QStringLiteral("fecha_ini")), QStringLiteral("11/08/2025"));
        QCOMPARE(value(QStringLiteral("resultado_control")), QStringLiteral("OBSERVADO"));   // la opción marcada
        QCOMPARE(value(QStringLiteral("obs_controlfuncional")).left(11), QStringLiteral("Se debe gen"));
        QCOMPARE(value(QStringLiteral("tot_obs_func")), QStringLiteral("1"));
        // Las correcciones van una sola vez y con el valor del sistema: la casilla visible la
        // deshabilita el script de la ventana y sólo viaja el oculto del final.
        QCOMPARE(count(QStringLiteral("tot_corr_func")), 1);
        QCOMPARE(value(QStringLiteral("tot_corr_func")), QStringLiteral("36"));
        // Los campos deshabilitados no se envían, y el adjunto lo pone quien envía.
        QCOMPARE(count(QStringLiteral("observaciones")), 0);
        QCOMPARE(count(QStringLiteral("obs_controlcalidad")), 0);
        QCOMPARE(count(QStringLiteral("arch_funcional")), 0);
    }

    void aFormWithoutItsFieldsIsAnError() {
        QString html = fixture("control_calidad_form.html");
        html.remove(QStringLiteral("<input type=\"hidden\" name=\"tip_control\" value=\"CALIDAD\">"));
        const gesreq::ControlForm form = gesreq::parseControlForm(html);
        QVERIFY(!form.ok);
        QVERIFY2(form.error.contains(QStringLiteral("tip_control")), qPrintable(form.error));
        // Y una página que no es el formulario, tampoco.
        QVERIFY(!gesreq::parseControlForm(fixture("bandeja_calidad.html")).ok);
    }

    void onlyTheStatesOfTheSystemCountAsSaved() {
        QVERIFY(gesreq::isSavedState(QStringLiteral("OK")));
        QVERIFY(gesreq::isSavedState(QStringLiteral("UPDATE-AJAX")));
        QVERIFY(!gesreq::isSavedState(QStringLiteral("ERROR")));
        QVERIFY(!gesreq::isSavedState(QString()));
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
