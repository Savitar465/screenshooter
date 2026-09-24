#pragma once

#include "core/models/Issue.h"
#include "core/models/RunHistory.h"
#include "core/services/IIssueRepository.h"

#include <QHash>
#include <QObject>
#include <functional>
#include <memory>

namespace qaflow {

class PlanStore;
class RunHistoryStore;

/// Issues del proyecto: fuente de verdad de la lista, de los planes que prueban cada requerimiento y
/// de lo importado del sistema de requerimientos. Persiste en cada cambio.
///
/// Lo importado y lo escrito en QAflow van por separado: volver a importar un requerimiento sólo
/// reemplaza lo extraído (y anota qué cambió); título, notas, prioridad, estado y asociaciones son de
/// QAflow y no se tocan. Un requerimiento se identifica por su número dentro de la conexión (la dirección
/// de GESREQ), así que importarlo otra vez nunca crea un segundo issue.
class IssueStore : public QObject {
    Q_OBJECT
public:
    explicit IssueStore(std::shared_ptr<IIssueRepository> repo, QObject* parent = nullptr);

    void load();
    /// Escribe en disco. Falso (y `saveFailed`) si no se pudo o si los datos no se pudieron leer al cargar.
    bool save();
    /// Los issues guardados no se pudieron leer: no se escribe encima para no perderlos.
    bool isReadOnly() const { return m_readOnly; }

    const QList<Issue>& issues() const { return m_issues; }
    const Issue* find(const QString& id) const;
    /// Issues que prueban ese plan (un plan puede agrupar las pruebas de varios).
    QList<Issue> issuesForPlan(const QString& planId) const;
    /// El issue de ese requerimiento en esa conexión; nullptr si todavía no se importó.
    const Issue* findByRequirement(const QString& connection, const QString& requirementId) const;
    /// Issues con cambios de GESREQ sin revisar.
    int changedCount() const;

    QString selectedId() const { return m_selectedId; }
    void select(const QString& id);

    QString createIssue(const QString& title);
    void updateIssue(const QString& id, const std::function<void(Issue&)>& mutate);
    /// Borra el issue; sus casos, planes y resultados no se tocan.
    void removeIssue(const QString& id);
    void linkPlan(const QString& issueId, const QString& planId);
    void unlinkPlan(const QString& issueId, const QString& planId);

    // ---- Flujo de la revisión ------------------------------------------------------------------
    /// El issue y la ronda a los que pertenece un ciclo de plan. Vacío = el ciclo no prueba ningún issue.
    struct RevisionRef {
        QString issueId;
        int revision = 0;
        QString phase;   // fase de esa ronda: el ambiente en el que se prueba

        bool isEmpty() const { return issueId.trimmed().isEmpty(); }
    };
    /// Empezó un ciclo de ese plan: los issues que lo agrupan pasan a «En pruebas» y, si no tenían
    /// ninguna revisión abierta, abren la siguiente (un requerimiento observado que vuelve a probarse
    /// es la revisión N+1 del acta). El avance automático nunca retrocede de estado por su cuenta.
    /// Devuelve el issue y la revisión a los que queda asociado el ciclo (el primero que agrupa el plan).
    /// `phase` es la fase en la que se arranca el ciclo (su ambiente): una ronda nueva nace en ella y una
    /// abierta en otra fase pasa a ella —quien arranca ya comprobó que esa ronda no tiene ciclos en su
    /// fase—. Vacía, o que no sea del issue, = la fase que le toca (`nextPhase`).
    RevisionRef notePlanStarted(const QString& planId, const QString& phase = QString());
    /// Lo que haría `notePlanStarted` sin hacerlo: el issue, la ronda y la fase a los que pertenecería un
    /// ciclo de ese plan que empezara ahora (la ronda abierta o la siguiente, con la fase que le toca).
    /// Es lo que el diálogo de arranque enseña y el ambiente con el que arranca el ciclo.
    RevisionRef nextCycleContext(const QString& planId) const;
    /// Fases del control de calidad del proyecto (`Project::phases`, ya resueltas con
    /// `normalizedQaPhases`). Deciden la fase de cada ronda nueva y qué ronda cierra el requerimiento.
    void setPhases(const QStringList& phases);
    const QStringList& phases() const { return m_phases; }
    /// Fases de ese issue: las suyas si se eligieron (en el orden del proyecto) o, si no, las del proyecto.
    QStringList phasesOf(const Issue& issue) const;
    /// Elige en qué fases se prueba el issue (sólo QA, sólo PRE…). Elegir todas las del proyecto, o
    /// ninguna, vuelve a «las del proyecto». No se puede quitar una fase con rondas **cerradas** (su acta y
    /// su resultado son de ella); la ronda abierta en una fase que se quita pasa a la siguiente que quede
    /// —quien lo pide ya comprobó que no tiene ciclos—. Devuelve vacío si se hizo y, si no, el motivo.
    QString setIssuePhases(const QString& issueId, const QStringList& phases);

    // ---- Zephyr --------------------------------------------------------------------------------
    /// Tests de Zephyr de los casos del issue (caso → clave), que se reutilizan en todos sus ciclos.
    void noteZephyrTests(const QString& issueId, const QHash<QString, QString>& tests);
    /// El ciclo de Zephyr de una fase del issue, con el nombre con el que se creó.
    void noteZephyrCycle(const QString& issueId, const QString& phase, const QString& cycleId, const QString& name);
    /// Olvida el ciclo de una fase (se borró en Zephyr): la próxima publicación crea otro.
    void forgetZephyrCycle(const QString& issueId, const QString& phase);
    /// Abre la ronda siguiente (la primera si no hay ninguna) y deja el issue «En pruebas».
    /// Devuelve su número, o 0 si el issue no existe.
    int openRevision(const QString& issueId);
    /// Guarda en una revisión lo escrito en el acta, con qué ciclo de plan se levantó y, si se generó,
    /// dónde quedó el fichero. `revision` es el número de la ronda; 0 (lo habitual), la última.
    void setRevisionRecord(const QString& issueId, const QualityRecord& record, const QString& documentPath = QString(),
                           const QString& planRunId = QString(), int revision = 0);
    /// Guarda lo que se hizo con el resultado en el gestor, en esa ronda (0 = la última).
    void setRevisionPublication(const QString& issueId, const RevisionPublication& publication, int revision = 0);
    /// Guarda el registro del resultado en GESREQ, en esa ronda (0 = la última).
    void setRevisionRegistration(const QString& issueId, const RevisionRegistration& registration, int revision = 0);
    /// Cierra la revisión en curso con su resultado. Sólo lo finaliza lo que cierra el requerimiento
    /// (`closesRequirement`: Conforme en la última fase); Conforme en otra fase aprueba ésa y el issue
    /// sigue en pruebas para la siguiente, y Observado lo devuelve a pruebas. Sin revisión abierta no
    /// hace nada.
    void closeRevision(const QString& issueId, QaOutcome outcome);

    // ---- Importación ---------------------------------------------------------------------------
    struct ImportCandidate {
        enum class Kind { New, Changed, Unchanged };
        ExternalRequirement requirement;
        Kind kind = Kind::New;
        QString issueId;                    // el issue que ya lo tiene, si no es nuevo
        QList<RequirementChange> changes;   // lo que traería actualizarlo
    };
    /// Cómo quedaría cada requerimiento leído: nuevo, con cambios o igual que el ya importado.
    QList<ImportCandidate> previewImport(const QList<ExternalRequirement>& requirements, const QString& connection) const;

    struct ImportResult {
        QStringList created;   // issues nuevos
        QStringList updated;   // issues que traían cambios
    };
    /// Crea un issue por requerimiento nuevo (con su descripción y prioridad como punto de partida) y
    /// actualiza lo extraído de los ya importados. Selecciona el primer issue creado.
    ImportResult importRequirements(const QList<ExternalRequirement>& requirements, const QString& connection,
                                    const QDateTime& fetchedAt = QDateTime::currentDateTime());
    /// Deja listo el issue desde el que se prueba ese requerimiento y lo selecciona: lo crea si es la
    /// primera vez y, si ya estaba importado, reutiliza el que hay (con sus planes y lo escrito en
    /// QAflow) actualizando sólo lo extraído. Devuelve su id, o vacío si el requerimiento no tiene número.
    QString openForRequirement(const ExternalRequirement& requirement, const QString& connection,
                               const QDateTime& fetchedAt = QDateTime::currentDateTime());
    /// Tras leer la bandeja completa: los importados de esa conexión que ya no están en ella quedan como
    /// ausentes (sin borrarse), y los que sí están, como presentes. Devuelve cuántos quedan ausentes.
    int markInboxRead(const QList<ExternalRequirement>& inbox, const QString& connection,
                      const QDateTime& fetchedAt = QDateTime::currentDateTime());
    /// Lo que hace leer la bandeja entera con los issues ya importados: marca los que salieron de ella (y
    /// los que siguen) y pone al día lo extraído de los que están, anotando qué cambió. No crea ninguno:
    /// eso es empezar las pruebas de un requerimiento (`openForRequirement`). Devuelve los que quedan
    /// ausentes y los que traían cambios.
    struct InboxResult {
        int missing = 0;
        QStringList updated;
    };
    InboxResult applyInbox(const QList<ExternalRequirement>& inbox, const QString& connection,
                           const QDateTime& fetchedAt = QDateTime::currentDateTime());
    /// Deja en el issue el estado con el que quedó su requerimiento tras registrar el control (lo dice
    /// GESREQ al guardarlo), como si se hubiera vuelto a leer la bandeja: es un dato extraído, no algo
    /// escrito en QAflow, y los cambios pendientes de ese campo dejan de estarlo porque este es el valor
    /// bueno. Sin estado, no hace nada.
    void noteRequirementState(const QString& issueId, const QString& state,
                              const QDateTime& when = QDateTime::currentDateTime());
    /// Guarda la ficha consultada del requerimiento del issue.
    void setRequirementDetail(const QString& issueId, const RequirementDetail& detail,
                              const QDateTime& fetchedAt = QDateTime::currentDateTime());
    /// Da por revisados los cambios que trajo GESREQ.
    void acknowledgeChanges(const QString& issueId);

    /// Casos que prueban el issue: los de sus planes, en el orden de éstos y sin repetir. El issue no
    /// tiene casos sueltos: lo que se prueba de un requerimiento son sus planes.
    static QStringList caseIdsOf(const Issue& issue, const PlanStore& plans);
    /// Ciclos ejecutados de los planes del issue, el más reciente primero. Con `since`, sólo los que
    /// empezaron a partir de ese momento (los de la revisión en curso).
    static QList<PlanRun> cyclesOf(const Issue& issue, const RunHistoryStore& history, const QDateTime& since = QDateTime());
    /// Ciclos de una ronda concreta: los que se anotaron con ese número de revisión al arrancar y, para
    /// los anteriores a que se anotara, los que empezaron mientras esa ronda estaba abierta. Con
    /// `revision` 0 son todos los del issue.
    static QList<PlanRun> cyclesOfRevision(const Issue& issue, const RunHistoryStore& history, int revision);
    /// Ejecuciones de los ciclos de esa ronda, la más reciente primero.
    static QList<RunRecord> runsOfRevision(const Issue& issue, const RunHistoryStore& history, int revision);
    /// Ejecuciones de los ciclos de los planes del issue, la más reciente primero. Una ejecución
    /// suelta del mismo caso, o dentro de un plan que no es del issue, no es un resultado suyo.
    static QList<RunRecord> runsOf(const Issue& issue, const RunHistoryStore& history, const QDateTime& since = QDateTime());
    /// Ejecuciones de esos ciclos, la más reciente primero.
    static QList<RunRecord> runsOfCycles(const QList<PlanRun>& cycles, const RunHistoryStore& history);

signals:
    void issuesChanged();
    void issueChanged(const QString& id);
    void selectionChanged(const QString& id);
    void saveFailed(const QString& what);
    /// Los issues guardados no se pudieron leer (fichero dañado o de otra versión).
    void loadFailed(const QString& message);

private:
    Issue* findMutable(const QString& id);
    /// La última revisión del issue, creando la primera si todavía no hay ninguna.
    /// La ronda de `issue` sobre la que se escribe: la del número que se pida o, con 0, la última (que
    /// se crea si el issue todavía no tiene ninguna). Una ronda que no existe devuelve la última: nada
    /// que se guarde puede quedarse sin sitio.
    IssueRevision& revisionFor(Issue& issue, int number = 0);
    /// Añade al issue la ronda siguiente, con la fase que le toca.
    const IssueRevision& appendRevision(Issue& issue, const QString& phase = QString()) const;
    /// `phase` si es una de las fases del issue (con su nombre canónico); vacía si no.
    QString validPhase(const Issue& issue, const QString& phase) const;
    QString nextId() const;
    void persist(const QString& changedId = QString());

    std::shared_ptr<IIssueRepository> m_repo;
    QList<Issue> m_issues;
    QString m_selectedId;
    bool m_readOnly = false;
    QStringList m_phases = defaultQaPhases();
};

} // namespace qaflow
