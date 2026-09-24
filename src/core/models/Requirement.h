#pragma once

#include "core/models/QualityRecord.h"   // ObservationCount: el resumen del acta que pide GESREQ

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QList>
#include <QMetaType>
#include <QPair>
#include <QString>
#include <QStringList>

namespace qaflow {

/// Conexión con el sistema del que QAflow importa los requerimientos (GESREQ). `url` es la de la
/// aplicación con su context path (http://servidor:7401/greq); `password`, como el token del gestor,
/// nunca se persiste en claro.
struct RequirementSourceSettings {
    QString url;
    QString user;
    QString password;
    bool connected = false;   // la última prueba de conexión con estos ajustes entró

    QString baseUrl() const;   // url sin barra final
    /// Dirección absoluta de una ruta de la aplicación ("publico.do?id=1") o de un enlace relativo de
    /// sus páginas, que lo son a la aplicación y no a la raíz del servidor.
    QString resolve(const QString& path) const;
};

/// Un requerimiento tal y como aparece en la bandeja de control de calidad: una fila de su tabla.
struct ExternalRequirement {
    QString id;               // número GREQ (2025175): el identificador estable en el sistema
    QString system;           // la columna tal cual: "SUMA TRANSITO-TRANSITOS"
    QString systemCode;       // "SUMA TRANSITO": el proyecto externo que se vincula a un proyecto QAflow
    QString systemName;       // "TRANSITOS"
    QString summary;          // descripción corta
    QDate requestedOn;
    QDate assignedFrom;       // periodo asignado para el control de calidad
    QDate assignedUntil;
    QString requestingUnit;   // sigla de la unidad ("GNN")
    QString requester;        // funcionario solicitante
    QString user;
    QString priority;         // "ALTA", tal cual lo escribe el sistema
    /// Estados vigentes en el orden en que los muestra: un requerimiento puede estar a la vez en
    /// "CONTROL DE CALIDAD OBSERVADO" y en "CONTROL FUNCIONAL".
    QStringList states;
    QString detailUrl;
};

/// Un par etiqueta/valor de la ficha del requerimiento, en texto plano.
struct RequirementField {
    QString label;
    QString value;
};

/// Un bloque de la ficha ("Datos Asignado", "Datos Control de Calidad Solicitado"…).
struct RequirementSection {
    QString title;
    QList<RequirementField> fields;
};

/// Un fichero enlazado desde la ficha; `label` dice qué es ("Archivo de respaldo inicial").
struct RequirementAttachment {
    QString label;
    QString fileName;
    QString url;
};

/// La ficha completa de un requerimiento.
struct RequirementDetail {
    QString id;
    QString requestType;      // "NUEVA FUNCIONALIDAD"
    QString reference;
    QString requestingUnit;   // nombre completo de la unidad
    QString requester;
    QString priority;
    QString state;
    QString systemCode;
    QString description;      // alcance del requerimiento en texto plano, con sus párrafos y viñetas
    QList<RequirementField> fields;          // todos los datos generales, también los que no tienen campo propio
    QList<RequirementSection> sections;
    QList<RequirementAttachment> attachments;
    QString url;

    /// Valor del primer campo con esa etiqueta, en los datos generales o en las secciones; vacío si no está.
    QString field(const QString& label) const;
};

/// Por qué no se pudo leer el sistema. Distinguirlo es lo que impide tomar una página de login o una
/// estructura nueva por una bandeja vacía.
enum class RequirementSourceFailure {
    None,
    Configuration,   // falta la dirección, el usuario o la contraseña, o la dirección no lleva al sistema
    Network,         // sin respuesta o error del servidor: merece la pena reintentar
    Credentials,     // el sistema rechazó el usuario o la contraseña, o no conserva la sesión
    NotFound,        // el requerimiento no existe o el usuario no puede verlo
    PageChanged,     // la página no tiene la estructura esperada: hay que revisar el extractor
    Rejected,        // el sistema entendió la petición y no la aceptó (sus reglas al registrar un resultado)
};

struct RequirementInboxResult {
    bool ok = false;
    QList<ExternalRequirement> requirements;
    QDateTime fetchedAt;
    RequirementSourceFailure failure = RequirementSourceFailure::None;
    QString error;

    bool retryable() const { return failure == RequirementSourceFailure::Network; }
};

struct RequirementDetailResult {
    bool ok = false;
    RequirementDetail detail;
    QDateTime fetchedAt;
    RequirementSourceFailure failure = RequirementSourceFailure::None;
    QString error;

    bool retryable() const { return failure == RequirementSourceFailure::Network; }
};

/// Un adjunto de la ficha descargado: el fichero tal cual lo sirve el sistema.
struct RequirementAttachmentResult {
    bool ok = false;
    QByteArray data;
    QString fileName;         // el del adjunto, o el que declaró el servidor
    RequirementSourceFailure failure = RequirementSourceFailure::None;
    QString error;

    bool retryable() const { return failure == RequirementSourceFailure::Network; }
};

/// Lo que QAflow registra en el sistema de requerimientos al terminar una revisión: el resultado del
/// control de calidad, el comentario que lo explica y el acta que lo respalda. Es la única escritura
/// que QAflow hace en GESREQ, y siempre a petición expresa.
struct RequirementRegistration {
    QString requirementId;     // número GREQ
    QString systemCode;        // sistema del requerimiento que se revisó: el que tiene el control de calidad
    /// Valor canónico del resultado ("Conforme" / "Observado"): lo traduce el conector a lo que espera
    /// el formulario del sistema.
    QString result;
    QString comment;
    QString attachmentPath;    // acta generada; vacío = se registra sin adjunto
    /// El resumen de observaciones del acta (las cinco clasificaciones A–E). GESREQ pide las mismas
    /// cinco cifras y las contrasta con el resultado, así que van tal y como las contó el acta.
    QList<ObservationCount> observations;
};

struct RequirementRegistrationResult {
    bool ok = false;
    /// Estado con el que queda el requerimiento tras registrar el control, tal y como lo devuelve el
    /// sistema («CONTROL DE CALIDAD OBSERVADO»); vacío si no lo dijo.
    QString state;
    RequirementSourceFailure failure = RequirementSourceFailure::None;
    QString error;
    /// El envío se cortó sin respuesta: puede haber quedado registrado igualmente, así que hay que
    /// comprobarlo en el sistema antes de repetirlo.
    bool uncertain = false;

    bool retryable() const { return failure == RequirementSourceFailure::Network; }
};

/// Un sistema del catálogo de GESREQ: el código con el que aparece en la bandeja y su nombre.
struct RequirementSystem {
    QString code;   // "SUMA TRANSITO"
    QString name;   // "TRANSITOS"
};

struct RequirementSystemsResult {
    bool ok = false;
    QList<RequirementSystem> systems;
    RequirementSourceFailure failure = RequirementSourceFailure::None;
    QString error;

    bool retryable() const { return failure == RequirementSourceFailure::Network; }
};

/// El requerimiento de una ficha, como si fuera una fila de la bandeja: es lo que se tiene de uno que se
/// busca por su número y no está asignado al usuario. Sin descripción corta en la ficha, el resumen es el
/// principio de su alcance.
ExternalRequirement requirementFromDetail(const RequirementDetail& detail);

/// Parte la columna "Sistema" ("SUMA TRANSITO-TRANSITOS") en código y nombre por el primer guion.
/// Sin guion, todo es código.
QPair<QString, QString> splitSystem(const QString& system);

} // namespace qaflow

// Un requerimiento viaja en señales: iniciar sus pruebas puede cambiar de proyecto.
Q_DECLARE_METATYPE(qaflow::ExternalRequirement)
