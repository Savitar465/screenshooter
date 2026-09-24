#include "IssueDirectory.h"

#include "application/IssueStore.h"
#include "application/ProjectStore.h"

#include <algorithm>

namespace qaflow {

IssueDirectory::IssueDirectory(ProjectStore& projects, RepositoryFactory repositoryFor, QObject* parent)
    : QObject(parent), m_projects(projects), m_repositoryFor(std::move(repositoryFor)) {
    // Un proyecto nuevo, uno renombrado o uno que se borra: lo leído del disco se vuelve a leer.
    connect(&m_projects, &ProjectStore::projectsChanged, this, [this]() {
        m_cache.clear();
        emit changed();
    });
}

void IssueDirectory::attach(const QString& projectId, IssueStore* store) {
    if (!store) return;
    m_stores.insert(projectId, store);
    m_cache.remove(projectId);
    connect(store, &IssueStore::issuesChanged, this, &IssueDirectory::changed);
    connect(store, &IssueStore::issueChanged, this, &IssueDirectory::changed);
}

QList<Issue> IssueDirectory::issuesOf(const QString& projectId) const {
    if (const QPointer<IssueStore> store = m_stores.value(projectId)) return store->issues();
    if (const auto cached = m_cache.constFind(projectId); cached != m_cache.cend()) return *cached;
    // Unos issues que no se pueden leer no se enseñan: es su proyecto el que avisa al abrirlo.
    const std::shared_ptr<IIssueRepository> repo = m_repositoryFor ? m_repositoryFor(projectId) : nullptr;
    const QList<Issue> loaded = repo ? repo->loadIssues().value_or(QList<Issue>{}) : QList<Issue>{};
    m_cache.insert(projectId, loaded);
    return loaded;
}

QList<IssueDirectory::Entry> IssueDirectory::issues(const QString& except) const {
    QList<Entry> out;
    for (const auto& project : m_projects.projects()) {
        if (project.id == except) continue;
        for (const auto& issue : issuesOf(project.id)) out << Entry{project.id, issue};
    }
    return out;
}

std::optional<IssueDirectory::Entry> IssueDirectory::findByRequirement(const QString& connection, const QString& requirementId) const {
    for (const auto& project : m_projects.projects())
        for (const auto& issue : issuesOf(project.id))
            if (issue.testsRequirement(connection, requirementId)) return Entry{project.id, issue};
    return std::nullopt;
}

void IssueDirectory::applyInbox(const QList<ExternalRequirement>& inbox, const QString& connection, const QDateTime& fetchedAt) {
    for (const auto& project : m_projects.projects()) {
        if (const QPointer<IssueStore> store = m_stores.value(project.id)) {
            store->applyInbox(inbox, connection, fetchedAt);
            continue;
        }
        // Sin sesión abierta nadie más tiene ese fichero entre manos: se pone al día con un store de paso,
        // que respeta lo mismo que el de la sesión (sólo lo extraído, y nada si no se pudo leer).
        const std::shared_ptr<IIssueRepository> repo = m_repositoryFor ? m_repositoryFor(project.id) : nullptr;
        if (!repo) continue;
        IssueStore store(repo);
        store.load();
        const bool imported = std::any_of(store.issues().cbegin(), store.issues().cend(),
                                          [&connection](const Issue& i) { return i.isImported() && sameConnection(i.requirement.connection, connection); });
        if (imported && !store.isReadOnly()) store.applyInbox(inbox, connection, fetchedAt);
        m_cache.insert(project.id, store.issues());
    }
    emit changed();
}

} // namespace qaflow
