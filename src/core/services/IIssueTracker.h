#pragma once

#include "core/models/BugReport.h"
#include "core/models/Settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QList>
#include <QString>
#include <QtGlobal>
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
    /// Lo que salió a medias sin invalidar la operación (el issue se creó pero no se pudo asignar…).
    QString warning;
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

/// Personas encontradas para el campo "Asignado a" mientras se escribe.
struct AssigneeSearch {
    bool ok = false;
    QList<Assignee> assignees;
    QString error;
};

/// Un proyecto del gestor: la clave con la que se configura y su nombre.
struct TrackerProject {
    QString key;        // SHOP
    QString name;       // Tienda online
};

/// Proyectos que ve el usuario en el gestor, para elegir el de un proyecto de QAflow.
struct TrackerProjectList {
    bool ok = false;
    QList<TrackerProject> projects;
    QString error;
};

/// Lo que QAflow manda al gestor al publicar un issue de trabajo (no un defecto): el issue de QAflow
/// decide el texto, y el gestor sólo lo traduce a sus campos.
struct TrackerIssueDraft {
    QString summary;
    QString description;
    QString issueType;     // tipo de incidencia del gestor ("Tarea", "Task", "Historia"…)
    QStringList labels;
};

/// Un issue tal y como está ahora en el gestor.
struct TrackerIssueInfo {
    bool ok = false;
    QString key;
    QString url;
    QString title;
    QString issueType;
    QString status;
    bool resolved = false;
    QDateTime createdAt;   // cuándo se creó en el gestor (lo trae la búsqueda de bugs)
    QStringList labels;    // etiquetas del gestor; QAflow reconoce en ellas el caso del que salió
    QString error;
    bool retryable = false;
};

/// Una página de bugs del gestor, del más reciente al más antiguo. `total` es cuántos hay en total
/// (los que caben y los que no) y `nextStart` desde dónde pedir la página siguiente; -1 = no hay más.
struct TrackerIssueList {
    bool ok = false;
    QList<TrackerIssueInfo> issues;
    int total = 0;
    int nextStart = -1;
    QString error;

    bool hasMore() const { return nextStart >= 0; }
};

/// Gestor de incidencias. Asíncrono: las llamadas devuelven por callback en el hilo principal.
class IIssueTracker {
public:
    virtual ~IIssueTracker() = default;
    virtual void testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) = 0;
    virtual void createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) = 0;
    virtual void fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) = 0;
    virtual void fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) = 0;

    /// ¿Sabe este gestor buscar personas en el servidor mientras se escribe? Si no, el formulario
    /// se queda con los asignables que trajo `fetchMetadata()` y los filtra en local.
    virtual bool canSearchAssignees(const TrackerSettings& s) const { Q_UNUSED(s); return false; }
    /// Busca personas a las que asignar en el proyecto configurado. `query` es lo escrito en el
    /// formulario; vacío pide las primeras del proyecto.
    virtual void searchAssignees(const TrackerSettings& s, const QString& query, std::function<void(const AssigneeSearch&)> done) {
        Q_UNUSED(s); Q_UNUSED(query);
        done(AssigneeSearch{});
    }

    /// ¿Sabe este gestor listar sus proyectos? Sólo Jira, que es el gestor cuyo proyecto se elige en la
    /// configuración de cada proyecto de QAflow.
    virtual bool canListProjects(const TrackerSettings& s) const { Q_UNUSED(s); return false; }
    /// Proyectos que ve el usuario de los ajustes, ordenados por nombre.
    virtual void fetchProjects(const TrackerSettings& s, std::function<void(const TrackerProjectList&)> done) {
        Q_UNUSED(s);
        done(TrackerProjectList{false, {}, QCoreApplication::translate("core", "Este gestor no permite elegir el proyecto de una lista")});
    }

    /// ¿Sabe este gestor publicar y mantener los issues de QAflow (no los defectos)? Sólo Jira, por ahora.
    virtual bool canPublishIssues(const TrackerSettings& s) const { Q_UNUSED(s); return false; }
    /// Crea en el gestor la representación del issue de QAflow, asignada al usuario de las credenciales
    /// (quien lo crea es quien lleva las pruebas). Si la asignación falla, el issue sigue creado y
    /// `IssueResult::warning` lo dice.
    virtual void publishIssue(const TrackerSettings& s, const TrackerIssueDraft& draft, std::function<void(const IssueResult&)> done) {
        Q_UNUSED(s); Q_UNUSED(draft);
        done(IssueResult{false, {}, {}, QCoreApplication::translate("core", "Este gestor no publica los issues de QAflow"), false, 0});
    }
    /// Issue que ya existe en el gestor, para vincularlo en vez de crear otro.
    virtual void fetchIssue(const TrackerSettings& s, const QString& key, std::function<void(const TrackerIssueInfo&)> done) {
        Q_UNUSED(s); Q_UNUSED(key);
        TrackerIssueInfo info;
        info.error = QCoreApplication::translate("core", "Este gestor no publica los issues de QAflow");
        done(info);
    }
    /// Reescribe en el gestor el título y la descripción del issue publicado.
    virtual void updateIssue(const TrackerSettings& s, const QString& key, const TrackerIssueDraft& draft,
                             std::function<void(const IssueResult&)> done) {
        Q_UNUSED(s); Q_UNUSED(key); Q_UNUSED(draft);
        done(IssueResult{false, {}, {}, QCoreApplication::translate("core", "Este gestor no publica los issues de QAflow"), false, 0});
    }

    /// ¿Sabe este gestor devolver los bugs que creó QAflow (los que llevan su etiqueta)? Es lo que
    /// hace falta para que la pantalla de bugs enseñe lo que hay en el gestor y no sólo lo que se
    /// reportó desde este equipo. Sólo Jira, por ahora.
    virtual bool canSearchIssues(const TrackerSettings& s) const { Q_UNUSED(s); return false; }
    /// Una página de los bugs que QAflow creó en el proyecto configurado, del más reciente al más
    /// antiguo: `max` como mucho, empezando por el que hace `startAt` (0 = el primero).
    virtual void searchProjectBugs(const TrackerSettings& s, int startAt, int max, std::function<void(const TrackerIssueList&)> done) {
        Q_UNUSED(s); Q_UNUSED(startAt); Q_UNUSED(max);
        TrackerIssueList out;
        out.error = QCoreApplication::translate("core", "Este gestor no sabe buscar los bugs de QAflow");
        done(out);
    }

    /// ¿Sabe este gestor comentar un issue y adjuntarle ficheros? Es lo que hace falta para dejar allí
    /// el resultado de una revisión con su acta. Sólo Jira, por ahora.
    virtual bool canCommentIssues(const TrackerSettings& s) const { Q_UNUSED(s); return false; }
    /// Añade un comentario al issue y, si se pasan, le adjunta esos ficheros. `IssueResult::key` es el
    /// issue comentado y `attachmentsUploaded`, cuántos adjuntos entraron.
    virtual void commentIssue(const TrackerSettings& s, const QString& key, const QString& body, const QStringList& attachments,
                              std::function<void(const IssueResult&)> done) {
        Q_UNUSED(s); Q_UNUSED(key); Q_UNUSED(body); Q_UNUSED(attachments);
        done(IssueResult{false, {}, {}, QCoreApplication::translate("core", "Este gestor no admite comentarios desde QAflow"), false, 0});
    }

    /// ¿Sabe este gestor enlazar dos issues entre sí? Es lo que hace falta para que el issue del
    /// requerimiento tenga colgados sus bugs y sus Tests. Sólo Jira, por ahora.
    virtual bool canLinkIssues(const TrackerSettings& s) const { Q_UNUSED(s); return false; }
    /// Enlaza dos issues del gestor («relacionado con»). Enlazar dos veces el mismo par no crea dos
    /// enlaces, así que repetir la publicación es inofensivo.
    virtual void linkIssues(const TrackerSettings& s, const QString& from, const QString& to,
                            std::function<void(const IssueResult&)> done) {
        Q_UNUSED(s); Q_UNUSED(from); Q_UNUSED(to);
        done(IssueResult{false, {}, {}, QCoreApplication::translate("core", "Este gestor no enlaza issues desde QAflow"), false, 0});
    }

    /// ¿Sabe este gestor cerrar un issue (llevarlo a un estado de «hecho»)? Es lo que hace falta para
    /// que el issue del requerimiento se cierre al publicar una revisión conforme. Sólo Jira, por ahora.
    virtual bool canCloseIssues(const TrackerSettings& s) const { Q_UNUSED(s); return false; }
    /// Lleva el issue a un estado resuelto con la transición que ofrezca el flujo. Si ya lo estaba no
    /// hace nada y responde bien: cerrar dos veces es inofensivo.
    virtual void closeIssue(const TrackerSettings& s, const QString& key, std::function<void(const IssueResult&)> done) {
        Q_UNUSED(s); Q_UNUSED(key);
        done(IssueResult{false, {}, {}, QCoreApplication::translate("core", "Este gestor no cierra issues desde QAflow"), false, 0});
    }
};

} // namespace qaflow
