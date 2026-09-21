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
    /// Jira lista los proyectos que ve el usuario: el de cada proyecto de QAflow se elige de ahí.
    bool canListProjects(const TrackerSettings& s) const override { Q_UNUSED(s); return true; }
    void fetchProjects(const TrackerSettings& s, std::function<void(const TrackerProjectList&)> done) override;
    /// Jira es, por ahora, el único gestor donde se publican los issues de QAflow.
    bool canPublishIssues(const TrackerSettings& s) const override { Q_UNUSED(s); return true; }
    void publishIssue(const TrackerSettings& s, const TrackerIssueDraft& draft, std::function<void(const IssueResult&)> done) override;
    void fetchIssue(const TrackerSettings& s, const QString& key, std::function<void(const TrackerIssueInfo&)> done) override;
    void updateIssue(const TrackerSettings& s, const QString& key, const TrackerIssueDraft& draft, std::function<void(const IssueResult&)> done) override;

    /// Jira sabe buscar por etiqueta: los bugs de QAflow se recuperan del propio Jira.
    bool canSearchIssues(const TrackerSettings& s) const override { Q_UNUSED(s); return true; }
    void searchProjectBugs(const TrackerSettings& s, int startAt, int max, std::function<void(const TrackerIssueList&)> done) override;

    bool canCommentIssues(const TrackerSettings& s) const override { Q_UNUSED(s); return true; }
    void commentIssue(const TrackerSettings& s, const QString& key, const QString& body, const QStringList& attachments,
                      std::function<void(const IssueResult&)> done) override;

    bool canLinkIssues(const TrackerSettings& s) const override { Q_UNUSED(s); return true; }
    void linkIssues(const TrackerSettings& s, const QString& from, const QString& to,
                    std::function<void(const IssueResult&)> done) override;

    /// Cierra con la transición del flujo que lleve a un estado de la categoría «hecho».
    bool canCloseIssues(const TrackerSettings& s) const override { Q_UNUSED(s); return true; }
    void closeIssue(const TrackerSettings& s, const QString& key, std::function<void(const IssueResult&)> done) override;

    /// De las transiciones que ofrece un issue (`GET …/transitions?expand=transitions.fields`), la que
    /// lo cierra: una que lleve a la categoría «done», mejor si se llama como un cierre («Cerrar»,
    /// «Close», «Done»…). Vacía si no hay ninguna. `resolution` es la resolución con la que se manda
    /// cuando la transición la pide, o vacía si no la pide.
    static QString closingTransition(const QJsonArray& transitions, QString* resolution = nullptr);

private:
    /// El usuario de las credenciales, tal y como Jira lo asigna: `accountId` en Cloud, `name` en
    /// Server. Se pregunta una vez por instancia y usuario.
    void withMyself(const TrackerSettings& s, std::function<void(const QString& id, const QString& error)> done);
    QString m_myself;      // el usuario ya resuelto
    QString m_myselfFor;   // instancia + usuario para los que vale
    /// Asigna el issue al usuario de las credenciales. No falla la operación de la que cuelga: si no se
    /// puede, lo deja dicho en `result.warning`.
    void assignToMyself(const TrackerSettings& s, IssueResult result, std::function<void(const IssueResult&)> done);
    /// Tipo de enlace con el que se relacionan dos issues, preguntado al servidor una vez: el nombre lo
    /// configura cada instancia («Relates», «Relacionada con»…), así que no se puede dar por supuesto.
    void withLinkType(const TrackerSettings& s, std::function<void(const QString& type, const QString& error)> done);
    QString m_linkType;    // el elegido; vacío mientras no se haya preguntado
    QString m_linkTypeFor; // instancia para la que vale
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
