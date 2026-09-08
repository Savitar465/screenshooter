#pragma once

#include "infrastructure/tracker/HttpTrackerClient.h"

#include <QJsonArray>

namespace qaflow {

/// Cliente REST de Jira (API v2, la que habla tanto Jira Cloud como Jira Server / Data Center 7 y 8).
/// La autenticación la decide `TrackerSettings::jiraAuth`:
///  - CloudToken  → Basic con correo y API token;
///  - ServerBasic → Basic con usuario y contraseña (Jira Server, única opción por debajo de la 8.14);
///  - ServerToken → Bearer con un token personal (Jira Server 8.14+).
/// En Server las personas se identifican por su nombre de usuario (`name`) y en Cloud por `accountId`.
class JiraClient : public HttpTrackerClient {
    Q_OBJECT
public:
    explicit JiraClient(QObject* parent = nullptr) : HttpTrackerClient(parent) {}

    void testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) override;
    void createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) override;
    void fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) override;
    void fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) override;
    /// Jira busca personas en el servidor: el formulario no se limita a los primeros del proyecto.
    bool canSearchAssignees(const TrackerSettings& s) const override { Q_UNUSED(s); return true; }
    void searchAssignees(const TrackerSettings& s, const QString& query, std::function<void(const AssigneeSearch&)> done) override;

private:
    QNetworkRequest request(const TrackerSettings& s, const QString& path) const;
    /// `GET /user/assignable/search` con el texto buscado: Jira Server filtra por `username` y Cloud por `query`.
    static QString assignableSearchPath(const TrackerSettings& s, const QString& query, int maxResults);
    /// Convierte la respuesta de usuarios de Jira en asignables (id = accountId en Cloud, name en Server).
    static QList<Assignee> assigneesFrom(const QJsonArray& users, bool accountIds);
    /// Falta algún dato para poder llamar a la API (URL, usuario o secreto); vacío si están todos.
    static QString missingCredentials(const TrackerSettings& s);
    /// Mensaje de error para mostrar: explica el rechazo de credenciales de Jira Server cuando lo hay.
    static QString errorFor(const TrackerSettings& s, const Response& r);
    void uploadAttachments(const TrackerSettings& s, IssueResult result, QStringList pending, std::function<void(const IssueResult&)> done);
};

} // namespace qaflow
