#pragma once

#include "core/models/Requirement.h"

#include <QString>

/// Extractor de las páginas de GESREQ: funciones puras de HTML a modelos, sin red. Todo lo que depende
/// del marcado del sistema vive aquí, así que un cambio en sus páginas se corrige en este fichero y en
/// sus fixtures (tests/fixtures/gesreq) sin tocar la sesión ni lo que se hace con los requerimientos.
namespace qaflow::gesreq {

/// Rutas de la aplicación, relativas a su context path.
inline constexpr const char* kLoginPath = "login.do";
inline constexpr const char* kInboxPath = "calidadreg.do";   // «Registro Control Calidad»
QString detailPath(const QString& id);

/// La página es el formulario de inicio de sesión: no hay sesión o ha caducado.
bool isLoginPage(const QString& html);
/// La página es de la aplicación con la sesión iniciada (tiene el enlace de cerrar sesión).
bool isAuthenticatedPage(const QString& html);
/// Usuario conectado tal y como lo muestra el menú ("Maidana Alvarado, Juan Jonas"); vacío si no está.
QString userName(const QString& html);
/// Motivo que enseña el formulario de login tras un intento rechazado; vacío si no dice ninguno.
QString loginMessage(const QString& html);

struct InboxPage {
    bool ok = false;
    QList<ExternalRequirement> requirements;
    QString error;   // qué no cuadra cuando la página no es la bandeja esperada
};
/// Tabla de la bandeja de control de calidad. Las columnas se buscan por su cabecera, no por su
/// posición, y la falta de una imprescindible es un error, no una columna vacía.
InboxPage parseInbox(const QString& html, const RequirementSourceSettings& s);

struct DetailPage {
    enum class Kind {
        Detail,       // la ficha del requerimiento pedido
        Empty,        // la cáscara sin número ni datos que GESREQ devuelve con HTTP 200 cuando no hay sesión
        Missing,      // la ficha con su número y sin ningún dato: con sesión, el requerimiento no existe
        Unexpected,   // otra cosa: `error` dice qué
    };
    Kind kind = Kind::Unexpected;
    RequirementDetail detail;
    QString error;
};
/// Ficha de `publico.do`, sin el historial de controles (las ventanas modales que la página lleva dentro).
DetailPage parseDetail(const QString& html, const QString& expectedId, const RequirementSourceSettings& s);

/// Páginas con el desplegable «Sistema» del catálogo, en orden de preferencia: la de seguimiento es un
/// informe de sólo lectura, y el registro adicional tiene el mismo catálogo para quien no vea la primera.
inline constexpr const char* kSystemsPaths[] = {"poai.do", "registroadicional.do"};

// ---- Registro del control de calidad ----------------------------------------------------------
//
// El camino que recorre el usuario: en la bandeja pulsa «Registrar», que lleva a la pantalla de gestión
// del requerimiento; allí cada sistema del requerimiento tiene un botón que abre en una ventana el
// formulario del control; al guardarlo, la página no envía el formulario a su propio `action` sino que
// lo manda por AJAX a `kControlSavePath`. QAflow hace lo mismo, pidiéndole al servidor cada enlace en
// vez de componerlo: la pantalla de gestión sólo ofrece el botón si se la pide con el `estado` con el
// que el requerimiento figura en la bandeja.

/// Destino del envío del formulario del control de calidad: no es el `action` del formulario (que su
/// propio script no usa), sino la ruta a la que lo manda `Anb.form.ajax`.
inline constexpr const char* kControlSavePath = "calidadregGestionControlGuardar.do";
/// Campo del adjunto en ese formulario (el acta del control).
inline constexpr const char* kControlFileField = "arch_funcional";
/// Lo que admite el campo de observaciones del control, como el que la propia página limita con
/// `maximo(this, 2000)`: pasarse lo rechaza el servidor, y el resumen del acta cabe de sobra.
inline constexpr int kControlCommentMax = 2000;

/// Enlace «Registrar» de la bandeja para ese requerimiento, tal y como lo escribe el servidor (con su
/// `estado`); vacío si la fila no lo ofrece porque el requerimiento no admite registro.
QString registrationPath(const QString& html, const QString& id);

/// Un sistema del requerimiento en la pantalla de gestión: cada uno tiene su propio control de calidad.
struct ControlItem {
    QString systemCode;   // "SUMA TRANSITO"
    QString systemName;   // "TRANSITOS"
    QString formPath;     // enlace del formulario del control de ese sistema
};
/// Los sistemas de la pantalla de gestión, en el orden de su tabla.
QList<ControlItem> parseControlItems(const QString& html);

struct ControlForm {
    bool ok = false;
    /// Los campos que enviaría el navegador, en el orden del formulario y ya sin los que su script
    /// deshabilita. El adjunto no está: lo pone quien envía.
    QList<QPair<QString, QString>> fields;
    /// Cuántos campos van antes del adjunto en el formulario: el navegador lo envía en su sitio, no al
    /// final, y hay servidores que leen las partes en orden. -1 = no había campo de fichero.
    int filePosition = -1;
    QString error;
};
/// El formulario del control de calidad (`#formc`). Reproduce lo que enviaría el navegador: se quedan
/// fuera los campos deshabilitados y los contadores de correcciones, que el script deshabilita en el
/// control de calidad y viajan en los ocultos del final con el valor que puso el sistema.
ControlForm parseControlForm(const QString& html);

/// El estado con el que responde el envío (`{"state":…}` o el estado suelto) es de los que `Anb.form.ajax`
/// da por guardados; cualquier otro es el motivo del rechazo, que la página enseña tal cual.
bool isSavedState(const QString& state);

/// Lo que explica una página de error del servidor (el 500 de Tomcat o el volcado de una JSP): su
/// mensaje, la excepción o la causa raíz, en una línea. Vacío si la página no dice nada aprovechable.
/// Sin esto, un fallo del servidor sólo se ve como «HTTP 500» y no hay por dónde empezar a mirar.
QString serverErrorReason(const QString& html);

struct SystemsPage {
    bool ok = false;
    QList<RequirementSystem> systems;
    QString error;
};
/// Catálogo de sistemas del desplegable `sistema` ("SUMA TRANSITO - TRANSITOS"), en el orden de la página.
SystemsPage parseSystems(const QString& html);

/// Texto plano de un fragmento HTML: una línea por párrafo, viñetas en las listas y entidades resueltas.
QString toPlainText(const QString& html);
/// Fecha dd/mm/aaaa del sistema; inválida si no lo es.
QDate parseDate(const QString& text);

} // namespace qaflow::gesreq
