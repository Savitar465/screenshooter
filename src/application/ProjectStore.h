#pragma once

#include "core/services/IProjectRepository.h"
#include <QObject>
#include <memory>

namespace qaflow {
/// Catálogo de proyectos. Cada proyecto posee sus propios repositorios y servicios.
class ProjectStore : public QObject {
    Q_OBJECT
public:
    explicit ProjectStore(std::shared_ptr<IProjectRepository> repo, QObject* parent = nullptr);
    bool load();
    const QList<Project>& projects() const { return m_collection.projects; }
    QString activeId() const { return m_collection.activeId; }
    const Project* find(const QString& id) const;
    QString dataDir(const QString& id) const;
    QString create(const QString& name);
    bool rename(const QString& id, const QString& name);
    bool setActive(const QString& id);
    const QStringList& suites() const { return m_collection.suites; }
    bool registerSuites(const QStringList& names);
    /// Vincula al proyecto el sistema de GESREQ cuyos requerimientos se trabajan en él (vacío lo
    /// desvincula). Falla, con `failed`, si ese sistema ya es de otro proyecto: al iniciar las pruebas de un
    /// requerimiento tiene que haber un único proyecto al que ir.
    bool setRequirementSystem(const QString& id, const QString& system);
    /// Proyecto vinculado a ese sistema de GESREQ (sin distinguir mayúsculas ni espacios repetidos), sin
    /// contar `except`; vacío si ninguno.
    QString projectForRequirementSystem(const QString& system, const QString& except = QString()) const;
    /// Fases del control de calidad del proyecto, ya resueltas (`normalizedQaPhases`): las suyas o las de
    /// por defecto. Un proyecto que no existe tiene las de por defecto.
    QStringList phasesOf(const QString& id) const;
    /// Cambia las fases del proyecto, en orden. Las vacías y repetidas se descartan; dejar las de por
    /// defecto (o ninguna) vuelve a ellas. Las rondas ya abiertas conservan la fase con la que nacieron.
    bool setPhases(const QString& id, const QStringList& phases);
signals:
    void projectsChanged();
    void suitesChanged();
    /// Notifica a las sesiones abiertas que deben recargar los ajustes generales.
    void settingsChanged(QObject* source);
    void activeChanged();
    void failed(const QString& message);
private:
    std::shared_ptr<IProjectRepository> m_repo;
    ProjectCollection m_collection;
};
} // namespace qaflow
