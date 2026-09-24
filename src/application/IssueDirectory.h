#pragma once

#include "core/models/Issue.h"
#include "core/services/IIssueRepository.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <functional>
#include <memory>
#include <optional>

namespace qaflow {

class IssueStore;
class ProjectStore;

/// Los issues de **todos** los proyectos, para la vista general del tablero y para saber, al leer la
/// bandeja de GESREQ, qué requerimientos tienen ya su issue y dónde.
///
/// Cada issue sigue siendo de su proyecto y sólo su `IssueStore` lo cambia. De los proyectos con la
/// sesión abierta se lee su store (`attach`), que es la fuente buena; de los demás, su `issues.json`, que
/// nadie más está escribiendo, leído una vez y guardado hasta que cambie el catálogo o se reescriba aquí.
class IssueDirectory : public QObject {
    Q_OBJECT
public:
    /// El repositorio de issues de un proyecto: lo da la raíz de composición, que es quien sabe dónde
    /// viven sus datos.
    using RepositoryFactory = std::function<std::shared_ptr<IIssueRepository>(const QString& projectId)>;

    IssueDirectory(ProjectStore& projects, RepositoryFactory repositoryFor, QObject* parent = nullptr);

    /// El store de la sesión abierta de un proyecto: desde ahora sus issues se leen de él.
    void attach(const QString& projectId, IssueStore* store);

    /// Un issue con el proyecto al que pertenece.
    struct Entry {
        QString projectId;
        Issue issue;
    };
    /// Issues de todos los proyectos del catálogo, en su orden, salvo los de `except`.
    QList<Entry> issues(const QString& except = QString()) const;
    /// El issue de ese requerimiento, en el proyecto que sea; vacío si ninguno lo tiene todavía.
    std::optional<Entry> findByRequirement(const QString& connection, const QString& requirementId) const;
    /// Tras leer la bandeja entera: los issues importados de todos los proyectos quedan al día —cuáles
    /// salieron de ella y qué cambió en los que siguen— como si cada proyecto la hubiera consultado.
    void applyInbox(const QList<ExternalRequirement>& inbox, const QString& connection,
                    const QDateTime& fetchedAt = QDateTime::currentDateTime());

signals:
    /// Cambió algún issue de algún proyecto (o el catálogo de proyectos).
    void changed();

private:
    /// Issues de un proyecto: de su store si tiene la sesión abierta; si no, del disco (y en caché).
    QList<Issue> issuesOf(const QString& projectId) const;

    ProjectStore& m_projects;
    RepositoryFactory m_repositoryFor;
    QHash<QString, QPointer<IssueStore>> m_stores;
    mutable QHash<QString, QList<Issue>> m_cache;
};

} // namespace qaflow
