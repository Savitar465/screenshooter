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
