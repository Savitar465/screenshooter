#pragma once

#include "core/models/BugReport.h"
#include "core/models/Settings.h"

#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

namespace qaflow {

struct IssueResult {
    bool ok = false;
    QString key;        // SHOP-143
    QString url;
    QString error;
    /// El fallo fue de red o del servidor (5xx), no del contenido: merece la pena reintentar.
    bool retryable = false;
    int attachmentsUploaded = 0;
};

struct ConnectionResult {
    bool ok = false;
    QString displayName;
    QString error;
};

struct IssueStatus {
    bool ok = false;
    QString status;     // texto del gestor: "To Do", "open", "Closed", "Active"…
    bool resolved = false;
    QString error;
};

/// Persona a la que se puede asignar un issue.
struct Assignee {
    QString id;         // accountId / login / id numérico / email
    QString name;       // nombre a mostrar
};

/// Valores válidos del proyecto para rellenar los campos del formulario.
struct ProjectMetadata {
    QStringList issueTypes;
    QStringList priorities;
    QStringList components;   // componentes (Jira) o etiquetas (GitHub/GitLab)
    QStringList versions;     // versiones (Jira) o milestones (GitHub/GitLab)
    QList<Assignee> assignees;
    bool isEmpty() const { return issueTypes.isEmpty() && priorities.isEmpty() && components.isEmpty() && versions.isEmpty() && assignees.isEmpty(); }
};

struct MetadataResult {
    bool ok = false;
    ProjectMetadata metadata;
    QString error;
};

/// Gestor de incidencias. Asíncrono: las llamadas devuelven por callback en el hilo principal.
class IIssueTracker {
public:
    virtual ~IIssueTracker() = default;
    virtual void testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) = 0;
    virtual void createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) = 0;
    virtual void fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) = 0;
    virtual void fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) = 0;
};

} // namespace qaflow
