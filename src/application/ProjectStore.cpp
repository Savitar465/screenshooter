#include "ProjectStore.h"
#include <QUuid>
#include <algorithm>

namespace qaflow {
ProjectStore::ProjectStore(std::shared_ptr<IProjectRepository> repo, QObject* parent)
    : QObject(parent), m_repo(std::move(repo)) {}

bool ProjectStore::load() {
    const auto loaded = m_repo->load();
    if (!loaded) { emit failed(tr("No se pudo leer el catálogo de proyectos.")); return false; }
    m_collection = *loaded;
    emit suitesChanged();
    emit projectsChanged();
    return true;
}
const Project* ProjectStore::find(const QString& id) const {
    for (const auto& p : projects()) if (p.id == id) return &p;
    return nullptr;
}
QString ProjectStore::dataDir(const QString& id) const { return find(id) ? m_repo->dataDir(id) : QString(); }
QString ProjectStore::create(const QString& name) {
    const QString clean = name.trimmed();
    if (clean.isEmpty()) return {};
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto next = m_collection;
    next.projects.append({id, clean});
    if (!m_repo->initialize(id) || !m_repo->save(next)) {
        emit failed(tr("No se pudo crear el proyecto. Comprueba la carpeta de datos."));
        return {};
    }
    m_collection = next;
    emit projectsChanged();
    return id;
}
bool ProjectStore::rename(const QString& id, const QString& name) {
    if (!find(id) || name.trimmed().isEmpty()) return false;
    auto next = m_collection;
    for (auto& p : next.projects) if (p.id == id) p.name = name.trimmed();
    if (!m_repo->save(next)) { emit failed(tr("No se pudo guardar el nombre del proyecto.")); return false; }
    m_collection = next;
    emit projectsChanged();
    return true;
}
bool ProjectStore::registerSuites(const QStringList& names) {
    auto next = m_collection;
    for (const auto& name : names)
        if (!name.trimmed().isEmpty() && !next.suites.contains(name)) next.suites.append(name);
    if (next.suites == m_collection.suites) return true;
    std::sort(next.suites.begin(), next.suites.end(), [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });
    if (!m_repo->save(next)) { emit failed(tr("No se pudo guardar el catálogo de suites.")); return false; }
    m_collection = next;
    emit suitesChanged();
    return true;
}
bool ProjectStore::setActive(const QString& id) {
    if (!find(id)) return false;
    if (id == activeId()) return true;
    auto next = m_collection;
    next.activeId = id;
    if (!m_repo->save(next)) { emit failed(tr("No se pudo guardar el proyecto activo.")); return false; }
    m_collection = next;
    emit activeChanged();
    return true;
}
} // namespace qaflow
