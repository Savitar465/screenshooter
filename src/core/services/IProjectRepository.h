#pragma once

#include <QList>
#include <QString>
#include <QStringList>
#include <optional>

namespace qaflow {
struct Project {
    QString id;
    QString name;
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
