#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace qaflow {
struct Project {
    QString id;
    QString name;
    /// Sistema de GESREQ ("SUMA TRANSITO") cuyos requerimientos se trabajan en este proyecto; vacío si
    /// ninguno. Un sistema sólo puede estar vinculado a un proyecto.
    QString requirementSystem;
    /// Fases del control de calidad de sus requerimientos, en orden ("QA", "PRE"). Vacío = las de por
    /// defecto (`defaultQaPhases()`); se resuelven con `normalizedQaPhases()`.
    QStringList phases;
};
struct ProjectCollection {
    QString activeId;
    QList<Project> projects;
    QStringList suites;
};

class IProjectRepository {
public:
    virtual ~IProjectRepository() = default;
    virtual std::optional<ProjectCollection> load() = 0;
    virtual bool save(const ProjectCollection& collection) = 0;
    virtual bool initialize(const QString& id) = 0;
    virtual QString dataDir(const QString& id) const = 0;
};
} // namespace qaflow
