#pragma once

#include "core/models/IssueLink.h"
#include "core/services/IBugRepository.h"

#include <QObject>
#include <memory>

namespace qaflow {

/// Libro de bugs: issues creados en el gestor (enlazados a su caso) y cola de bugs pendientes
/// de envío. Fuente de verdad de ambas listas; persiste en cada cambio.
class BugStore : public QObject {
    Q_OBJECT
public:
    explicit BugStore(std::shared_ptr<IBugRepository> repo, QObject* parent = nullptr);

    void load();

    // Issues creados
    const QList<IssueLink>& issues() const { return m_ledger.issues; }
    /// Issues de un caso, el más reciente primero.
    QList<IssueLink> issuesForCase(const QString& caseId) const;
    const IssueLink* findIssue(const QString& key) const;
    int openIssueCount() const;
    void recordIssue(const IssueLink& link);
    void updateStatus(const QString& key, const QString& status, bool resolved);
    void forgetIssue(const QString& key);

    // Cola de pendientes
    const QList<PendingBug>& pending() const { return m_ledger.pending; }
    const PendingBug* findPending(const QString& id) const;
    /// Encola un bug que no se pudo enviar. Devuelve el id asignado.
    QString enqueue(const BugReport& report, const QString& error);
    void markAttempt(const QString& id, const QString& error);
    void removePending(const QString& id);

    /// Escribe el libro en disco. Falso (y `saveFailed`) si no se pudo.
    bool save();

signals:
    void bugsChanged();
    void saveFailed(const QString& what);

private:
    void persist();

    std::shared_ptr<IBugRepository> m_repo;
    BugLedger m_ledger;
};

} // namespace qaflow
