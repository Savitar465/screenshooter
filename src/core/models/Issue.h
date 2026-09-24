#pragma once

#include "core/models/QualityRecord.h"
#include "core/models/Requirement.h"
#include "core/models/TestCase.h"   // Priority

#include <QDateTime>
#include <QList>
#include <QMap>
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

/// Resultado del control de calidad de una ronda de pruebas: es lo que se lleva al acta, a Jira y a
/// GESREQ. No es el estado del issue, aunque lo mueve: sólo Conforme lo finaliza; Observado lo
/// devuelve a pruebas hasta que se abra la revisión siguiente.
enum class QaOutcome { Pendiente, Conforme, Observado };

/// Valor canónico (se persiste en issues.json y viaja a GESREQ). No traducir.
QString toString(QaOutcome o);
QaOutcome qaOutcomeFromString(const QString& s);
/// Texto para mostrar: Pendiente, Conforme, Observado.
QString label(QaOutcome o);

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

/// Lo que se hizo con el resultado de una revisión en el gestor: el comentario con el resumen y el
/// acta adjunta al issue ya publicado. Sigue las reglas de `IssuePublication`: nada se manda solo y
/// un envío cortado queda sin confirmar.
struct RevisionPublication {
    QString key;                 // issue del gestor en el que quedó el resultado
    QDateTime publishedAt;
    bool attachedDocument = false;
    bool uncertain = false;
    QString lastError;

    bool isEmpty() const { return !publishedAt.isValid() && !uncertain; }
};

/// El registro del resultado en GESREQ: la única escritura que QAflow hace en el sistema de
/// requerimientos, siempre a petición expresa.
struct RevisionRegistration {
    QDateTime registeredAt;
    QaOutcome result = QaOutcome::Pendiente;
    QString comment;
    /// Estado con el que quedó el requerimiento en GESREQ al registrar («CONTROL DE CALIDAD OBSERVADO»).
    /// Lo dice el propio sistema al guardar, así que el issue no necesita volver a consultarlo.
    QString requirementState;
    bool attachedDocument = false;
    bool uncertain = false;      // el envío se cortó: puede haberse registrado igualmente
    QString lastError;

    bool isEmpty() const { return !registeredAt.isValid() && !uncertain; }
};

/// Una ronda de control de calidad del requerimiento: se abre al empezar a probar y se cierra con su
/// resultado (Conforme u Observado), su acta y lo que se hizo con ella. Un requerimiento observado
/// vuelve a pruebas y abre la revisión siguiente, que es el «Número de Revisión» del acta.
///
/// Cada ronda es de una **fase** (el ambiente en el que se prueba: QA, PRE…). Conforme en una fase que no
/// es la última aprueba esa fase y la ronda siguiente es de la otra; sólo Conforme en la última cierra el
/// control del requerimiento (ver `closesRequirement`).
struct IssueRevision {
    int number = 1;
    /// Fase de la ronda ("QA", "PRE"). Vacía en las rondas anteriores a que existieran las fases (ver
    /// `phaseOf`).
    QString phase;
    QDateTime startedAt;
    QDateTime closedAt;                        // inválida mientras la revisión sigue abierta
    QaOutcome outcome = QaOutcome::Pendiente;
    /// Ciclo del plan (`PlanRun::id`) del que habla el acta. Una revisión puede tener varios ciclos
    /// (se repitieron las pruebas); el acta se genera con el que se elija y aquí queda cuál fue, para
    /// que regenerarla, mandarla a Jira o registrarla en GESREQ hablen todos del mismo. Vacío = el
    /// último ciclo de la revisión.
    QString planRunId;
    QualityRecord record;                      // lo escrito en el acta, para regenerarla sin teclearla otra vez
    QString documentPath;                      // acta generada
    QDateTime documentAt;
    RevisionPublication jira;
    RevisionRegistration gesreq;

    bool isOpen() const { return !closedAt.isValid(); }
    bool hasDocument() const { return !documentPath.trimmed().isEmpty(); }
};

/// Lo que el requerimiento tiene en Zephyr: **un Test por caso**, el mismo en todos sus ciclos, y **un
/// ciclo por fase** («GREQ 2026997 · QA», «GREQ 2026997 · PRE»), al que van todas las rondas y
/// continuaciones de esa fase actualizando sus ejecuciones. Las fases se guardan en mayúsculas.
struct IssueZephyr {
    QMap<QString, QString> tests;        // caso de QAflow → clave del Test (TC-104 → SHOP-77)
    QMap<QString, QString> cycles;       // fase → id del ciclo en Zephyr
    QMap<QString, QString> cycleNames;   // fase → nombre con el que se creó (su enlace lo busca por nombre)

    bool isEmpty() const { return tests.isEmpty() && cycles.isEmpty(); }
    static QString phaseKey(const QString& phase) { return phase.trimmed().toUpper(); }
    QString cycleOf(const QString& phase) const { return cycles.value(phaseKey(phase)); }
    QString cycleNameOf(const QString& phase) const { return cycleNames.value(phaseKey(phase)); }
};

/// Issue de QAflow: organiza el trabajo de QA de un requerimiento (o de algo que se crea a mano). Su
/// identidad es local e independiente de la clave de Jira, que se le añade al publicarlo.
struct Issue {
    QString id;                    // IS-0001
    QString title;
    QString notes;                 // lo escrito en QAflow; lo extraído vive en `requirement`
    Priority priority = Priority::Media;
    IssueState state = IssueState::Pending;
    /// Planes que prueban el requerimiento: el issue se organiza por planes, y sus casos son los de
    /// éstos (`IssueStore::caseIdsOf`). No hay casos sueltos colgando del issue.
    QStringList planIds;
    RequirementLink requirement;   // vacío en un issue creado a mano
    IssuePublication publication;  // vacío mientras no se publique ni se vincule
    /// Rondas de control de calidad, de la primera a la última. Vacío mientras no se haya empezado.
    QList<IssueRevision> revisions;
    /// Fases en las que se prueba este requerimiento, si no son todas las del proyecto: hay los que sólo
    /// pasan por QA, o sólo por PRE. Vacío = las del proyecto (`IssueStore::phasesOf`).
    QStringList phases;
    IssueZephyr zephyr;
    QDateTime createdAt;
    QDateTime updatedAt;

    bool isImported() const { return !requirement.isEmpty(); }
    bool isPublished() const { return !publication.isEmpty(); }
    /// ¿Es el issue de ese requerimiento de esa conexión? Un requerimiento se identifica por su número
    /// dentro de la dirección de GESREQ (sin distinguir la barra final ni mayúsculas).
    bool testsRequirement(const QString& connection, const QString& requirementId) const;
    /// La revisión en curso; nullptr si no hay ninguna abierta (o todavía ninguna).
    const IssueRevision* currentRevision() const;
    /// La ronda con ese número; nullptr si el issue no la tiene. Con `number <= 0`, la ronda de la que
    /// se habla por defecto: la abierta o, si ninguna lo está, la última. Es lo que deja publicar el
    /// resultado de una ronda anterior que se quedó a medias.
    const IssueRevision* revision(int number) const;
    /// Número de la ronda en curso (la abierta o la última cerrada); 0 si el issue no tiene ninguna.
    int currentRevisionNumber() const;
    /// La última revisión cerrada; nullptr si ninguna lo está.
    const IssueRevision* lastClosedRevision() const;
    /// Resultado de la última revisión cerrada; Pendiente si no hay ninguna.
    QaOutcome lastOutcome() const;
    /// Cuándo quedó finalizado: el cierre de su última revisión o, si se finalizó a mano sin cerrar
    /// ninguna, su último cambio. Inválida si el issue no está Finalizado.
    QDateTime finishedAt() const;
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

/// La misma dirección de GESREQ con o sin barra final, o con otras mayúsculas, es la misma conexión.
bool sameConnection(const QString& a, const QString& b);
/// Prioridad de QAflow a partir de la que escribe el sistema externo ("ALTA", "Media"…); Media si no se reconoce.
Priority priorityFromRequirement(const QString& text);
/// Qué cambió entre dos lecturas del mismo requerimiento, en un orden estable. Los espacios repetidos no cuentan.
QList<RequirementChange> diffRequirement(const ExternalRequirement& before, const ExternalRequirement& after);
/// Acumula cambios nuevos sobre los pendientes de revisar: de cada campo se conserva el valor de antes más
/// antiguo (el último revisado) y el de después más reciente; si vuelve a como estaba, desaparece.
QList<RequirementChange> mergeChanges(const QList<RequirementChange>& pending, const QList<RequirementChange>& incoming);
/// Nombre para mostrar de un campo de `RequirementChange` ("Estado", "Descripción corta"…).
QString requirementFieldLabel(const QString& field);

// ---- Fases del control de calidad ----------------------------------------------------------------
// Un requerimiento se prueba en fases ordenadas (por defecto QA y después PRE), cada una en una o más
// rondas. Las fases son del proyecto (`Project::phases`); estas funciones reciben la lista ya resuelta.

/// Las fases de un proyecto que no dice ninguna: QA y, aprobado ahí, PRE.
QStringList defaultQaPhases();
/// La lista tal y como se usa: sin espacios alrededor, sin vacías ni repetidas (sin distinguir
/// mayúsculas) y, si no queda ninguna, las de por defecto.
QStringList normalizedQaPhases(const QStringList& phases);
/// Fase de una ronda: la suya o, en una anterior a las fases, la última si quedó Conforme (entonces eso
/// cerraba el requerimiento) y la primera si no.
QString phaseOf(const IssueRevision& revision, const QStringList& phases);
/// ¿Es la última fase? Una fase que ya no está en la lista (se quitó de la configuración) cuenta como
/// última: no hay a dónde avanzar desde ella.
bool isFinalPhase(const QString& phase, const QStringList& phases);
/// Fase de la próxima ronda del issue: la primera si no tiene ninguna; la misma si la última sigue
/// abierta o quedó observada; la siguiente si la última aprobó una fase que no es la final.
QString nextPhase(const Issue& issue, const QStringList& phases);
/// ¿Cierra esta ronda el control de calidad del requerimiento? Sólo Conforme en la última fase: es lo
/// único que registra el «OK» en GESREQ, finaliza el issue y cierra su issue del gestor.
bool closesRequirement(const IssueRevision& revision, const QStringList& phases);
/// Texto del resultado de una ronda de esa fase: Conforme en una fase que no es la última se lee
/// «Aprobada en QA»; el resto, como `label(QaOutcome)`.
QString outcomeLabel(QaOutcome outcome, const QString& phase, const QStringList& phases);

} // namespace qaflow
