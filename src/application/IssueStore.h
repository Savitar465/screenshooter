#pragma once

#include "core/models/Issue.h"
#include "core/models/RunHistory.h"
#include "core/services/IIssueRepository.h"

#include <QObject>
#include <functional>
#include <memory>

namespace qaflow {

class RunHistoryStore;

/// Issues del proyecto: fuente de verdad de la lista, de sus asociaciones con casos y planes y de lo
/// importado del sistema de requerimientos. Persiste en cada cambio.
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
    /// Issues que valida un caso (un caso puede cubrir varios).
    QList<Issue> issuesForCase(const QString& caseId) const;
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
    void linkCase(const QString& issueId, const QString& caseId);
    void unlinkCase(const QString& issueId, const QString& caseId);
    void linkPlan(const QString& issueId, const QString& planId);
    void unlinkPlan(const QString& issueId, const QString& planId);

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
    /// primera vez y, si ya estaba importado, reutiliza el que hay (con sus casos, planes y lo escrito en
    /// QAflow) actualizando sólo lo extraído. Devuelve su id, o vacío si el requerimiento no tiene número.
    QString openForRequirement(const ExternalRequirement& requirement, const QString& connection,
                               const QDateTime& fetchedAt = QDateTime::currentDateTime());
    /// Tras leer la bandeja completa: los importados de esa conexión que ya no están en ella quedan como
    /// ausentes (sin borrarse), y los que sí están, como presentes. Devuelve cuántos quedan ausentes.
    int markInboxRead(const QList<ExternalRequirement>& inbox, const QString& connection,
                      const QDateTime& fetchedAt = QDateTime::currentDateTime());
    /// Guarda la ficha consultada del requerimiento del issue.
    void setRequirementDetail(const QString& issueId, const RequirementDetail& detail,
                              const QDateTime& fetchedAt = QDateTime::currentDateTime());
    /// Da por revisados los cambios que trajo GESREQ.
    void acknowledgeChanges(const QString& issueId);

    /// Ejecuciones de los casos del issue, la más reciente primero.
    static QList<RunRecord> runsOf(const Issue& issue, const RunHistoryStore& history);

signals:
    void issuesChanged();
    void issueChanged(const QString& id);
    void selectionChanged(const QString& id);
    void saveFailed(const QString& what);
    /// Los issues guardados no se pudieron leer (fichero dañado o de otra versión).
    void loadFailed(const QString& message);

private:
    Issue* findMutable(const QString& id);
    QString nextId() const;
    void persist(const QString& changedId = QString());

    std::shared_ptr<IIssueRepository> m_repo;
    QList<Issue> m_issues;
    QString m_selectedId;
    bool m_readOnly = false;
};

} // namespace qaflow
