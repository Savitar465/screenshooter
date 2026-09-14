#pragma once

#include "core/models/Requirement.h"
#include "core/models/TestCase.h"   // Priority

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace qaflow {

/// Estado del trabajo de QA en QAflow. Es independiente del estado del requerimiento en GESREQ, del de
/// su representación en Jira y del resultado de las pruebas: cerrar el issue en Jira no aprueba nada.
enum class IssueState { Pending, Preparing, Testing, Done };

/// Valor canónico (se persiste en issues.json). No traducir.
QString toString(IssueState s);
IssueState issueStateFromString(const QString& s);
/// Texto para mostrar: Pendiente, En preparación, En pruebas, Finalizado.
QString label(IssueState s);

/// Un dato del requerimiento que cambió en el sistema desde la última vez que se revisó en QAflow.
struct RequirementChange {
    QString field;    // clave estable: "states", "summary", "priority"… (ver requirementFieldLabel)
    QString before;
    QString after;
};

/// Lo que se trajo del sistema de requerimientos. No se edita en QAflow: se reemplaza al volver a
/// consultarlo, y lo que cambió queda en `changes` hasta que alguien lo revisa.
struct RequirementLink {
    QString connection;              // dirección de GESREQ de la que salió (http://servidor:7401/greq)
    ExternalRequirement data;        // la fila de la bandeja tal y como se leyó la última vez
    RequirementDetail detail;        // la ficha, si se consultó (`detail.id` vacío si no)
    QDateTime importedAt;
    QDateTime fetchedAt;             // última vez que se vio en la bandeja
    QDateTime detailFetchedAt;
    /// En la última lectura de la bandeja ya no estaba: el control de calidad terminó o se reasignó.
    /// El issue y sus pruebas se conservan igual.
    bool missing = false;
    QList<RequirementChange> changes;

    bool isEmpty() const { return data.id.isEmpty(); }
};

/// La representación del issue en el gestor (Jira): dónde está, qué se envió y qué se sabe de ella.
/// `publishedTitle` y `publishedDescription` son lo último que salió de QAflow: comparándolos con lo que
/// hay ahora se sabe si el issue está pendiente de actualizar, sin tocar lo que alguien editara en Jira.
struct IssuePublication {
    QString tracker;               // "Jira": gestor de destino
    QString baseUrl;               // instancia a la que se publicó
    QString project;               // clave del proyecto de destino (SHOP)
    QString key;                   // QA-12; vacía = sin publicar
    QString url;
    QString issueType;             // tipo con el que se creó
    QDateTime publishedAt;
    QString publishedTitle;
    QString publishedDescription;
    QString status;                // último estado conocido en el gestor; vacío = nunca consultado
    bool resolved = false;
    QDateTime statusCheckedAt;
    /// Se vinculó a un issue que ya existía en el gestor en vez de crearlo desde QAflow.
    bool linked = false;
    /// El último envío se cortó sin respuesta: puede haberse creado igualmente, así que hay que
    /// comprobarlo en el gestor antes de volver a intentarlo.
    bool uncertain = false;
    QString lastError;

    bool isEmpty() const { return key.trimmed().isEmpty(); }
};

/// Issue de QAflow: organiza el trabajo de QA de un requerimiento (o de algo que se crea a mano). Su
/// identidad es local e independiente de la clave de Jira, que se le añade al publicarlo.
struct Issue {
    QString id;                    // IS-0001
    QString title;
    QString notes;                 // lo escrito en QAflow; lo extraído vive en `requirement`
    Priority priority = Priority::Media;
    IssueState state = IssueState::Pending;
    QStringList caseIds;           // casos que lo validan; un caso puede cubrir varios issues
    QStringList planIds;           // planes que agrupan sus pruebas
    RequirementLink requirement;   // vacío en un issue creado a mano
    IssuePublication publication;  // vacío mientras no se publique ni se vincule
    QDateTime createdAt;
    QDateTime updatedAt;

    bool isImported() const { return !requirement.isEmpty(); }
    bool isPublished() const { return !publication.isEmpty(); }
    /// Texto en el que busca el filtro: id, título, notas, clave del gestor y lo importado (número,
    /// sistema, descripción, estados, solicitante, referencia y alcance).
    QString searchText() const;
};

/// Filtro de la lista de issues. Lo que no se indica no filtra.
struct IssueFilter {
    QString text;
    std::optional<IssueState> state;
    std::optional<Priority> priority;
    std::optional<bool> published;   // true = con representación en el gestor

    bool isEmpty() const { return text.trimmed().isEmpty() && !state && !priority && !published; }
    /// Cada palabra de `text` tiene que aparecer en `Issue::searchText()`.
    bool matches(const Issue& issue) const;
};

/// Prioridad de QAflow a partir de la que escribe el sistema externo ("ALTA", "Media"…); Media si no se reconoce.
Priority priorityFromRequirement(const QString& text);
/// Qué cambió entre dos lecturas del mismo requerimiento, en un orden estable. Los espacios repetidos no cuentan.
QList<RequirementChange> diffRequirement(const ExternalRequirement& before, const ExternalRequirement& after);
/// Acumula cambios nuevos sobre los pendientes de revisar: de cada campo se conserva el valor de antes más
/// antiguo (el último revisado) y el de después más reciente; si vuelve a como estaba, desaparece.
QList<RequirementChange> mergeChanges(const QList<RequirementChange>& pending, const QList<RequirementChange>& incoming);
/// Nombre para mostrar de un campo de `RequirementChange` ("Estado", "Descripción corta"…).
QString requirementFieldLabel(const QString& field);

} // namespace qaflow
