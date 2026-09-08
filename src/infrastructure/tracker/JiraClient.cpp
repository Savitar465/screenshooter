#include "JiraClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

namespace qaflow {

QNetworkRequest JiraClient::request(const TrackerSettings& s, const QString& path) const {
    QNetworkRequest req = jsonRequest(s.baseUrl() + path);
    if (s.jiraAuth == JiraAuth::ServerToken)
        req.setRawHeader("Authorization", "Bearer " + s.token.toUtf8());
    else   // correo + API token (Cloud) o usuario + contraseña (Server): el mismo Basic auth
        req.setRawHeader("Authorization", "Basic " + (s.user.trimmed() + QLatin1Char(':') + s.token).toUtf8().toBase64());
    return req;
}

QString JiraClient::missingCredentials(const TrackerSettings& s) {
    if (s.url.trimmed().isEmpty())
        return QCoreApplication::translate("infrastructure", "Indica la URL de Jira");
    if (s.needsUser() && s.user.trimmed().isEmpty())
        return QCoreApplication::translate("infrastructure", "Indica %1 y %2").arg(s.userLabel().toLower(), s.secretLabel().toLower());
    if (s.token.trimmed().isEmpty())
        return QCoreApplication::translate("infrastructure", "Indica %1").arg(s.secretLabel().toLower());
    return {};
}

QString JiraClient::errorFor(const TrackerSettings& s, const Response& r) {
    if (r.status != 401 && r.status != 403) return r.error;
    // Jira Server (Seraph) explica en cabeceras por qué rechaza el login: tras varios intentos fallidos
    // bloquea al usuario y exige resolver un CAPTCHA en el navegador antes de volver a aceptar la API.
    const QByteArray denied = r.header("X-Authentication-Denied-Reason");
    const QByteArray reason = r.header("X-Seraph-LoginReason");
    if (denied.contains("CAPTCHA") || reason.contains("AUTHENTICATION_DENIED"))
        return QCoreApplication::translate("infrastructure",
                                           "Jira ha bloqueado el acceso tras varios intentos fallidos: entra en %1 desde el navegador, "
                                           "resuelve el CAPTCHA y vuelve a probar.").arg(s.baseUrl());
    if (r.status == 401) {
        switch (s.jiraAuth) {
            case JiraAuth::ServerBasic:
                return QCoreApplication::translate("infrastructure", "Usuario o contraseña incorrectos.");
            case JiraAuth::ServerToken:
                return QCoreApplication::translate("infrastructure",
                                                   "Token personal no válido. Jira Server sólo admite tokens desde la versión 8.14; "
                                                   "en versiones anteriores usa usuario y contraseña.");
            case JiraAuth::CloudToken:
                return QCoreApplication::translate("infrastructure", "Correo o API token incorrectos.");
        }
    }
    return QCoreApplication::translate("infrastructure", "Sin permisos en Jira para esta operación (%1).").arg(r.error);
}

QString JiraClient::assignableSearchPath(const TrackerSettings& s, const QString& query, int maxResults) {
    QString path = QStringLiteral("/rest/api/2/user/assignable/search?project=%1&maxResults=%2")
                       .arg(s.project.trimmed()).arg(maxResults);
    // Jira Cloud sustituyó `username` por `query`; Jira Server / Data Center sigue con `username`.
    const QString text = query.trimmed();
    if (!text.isEmpty())
        path += (s.usesAccountId() ? QStringLiteral("&query=") : QStringLiteral("&username="))
                + QString::fromUtf8(QUrl::toPercentEncoding(text));
    return path;
}

QList<Assignee> JiraClient::assigneesFrom(const QJsonArray& users, bool accountIds) {
    QList<Assignee> out;
    for (const auto& v : users) {
        const QJsonObject u = v.toObject();
        // Jira Server permite desactivar cuentas sin borrarlas: no se puede asignar a ellas.
        if (u.contains(QStringLiteral("active")) && !u[QStringLiteral("active")].toBool()) continue;
        Assignee a;
        a.id = accountIds ? u[QStringLiteral("accountId")].toString() : u[QStringLiteral("name")].toString();
        a.name = u[QStringLiteral("displayName")].toString();
        if (a.name.isEmpty()) a.name = a.id;
        if (!a.id.isEmpty()) out.append(a);
    }
    return out;
}

void JiraClient::searchAssignees(const TrackerSettings& s, const QString& query, std::function<void(const AssigneeSearch&)> done) {
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) { done(AssigneeSearch{false, {}, missing}); return; }
    if (s.project.trimmed().isEmpty()) {
        done(AssigneeSearch{false, {}, QCoreApplication::translate("infrastructure", "Indica la clave del proyecto")});
        return;
    }
    const bool accountIds = s.usesAccountId();
    get(request(s, assignableSearchPath(s, query, 50)), [s, accountIds, done](const Response& r) {
        if (!r.ok) { done(AssigneeSearch{false, {}, errorFor(s, r)}); return; }
        done(AssigneeSearch{true, assigneesFrom(r.json.array(), accountIds), {}});
    });
}

void JiraClient::testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) {
    if (const QString missing = missingCredentials(s); !missing.isEmpty()) {
        done(ConnectionResult{false, {}, missing});
        return;
    }
    get(request(s, QStringLiteral("/rest/api/2/myself")), [s, done](const Response& r) {
        if (!r.ok) { done(ConnectionResult{false, {}, errorFor(s, r)}); return; }
        done(ConnectionResult{true, r.json.object()[QStringLiteral("displayName")].toString(), {}});
    });
}

void JiraClient::createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) {
    QJsonObject fields{
        {"project", QJsonObject{{"key", s.project.trimmed()}}},
        {"issuetype", QJsonObject{{"name", bug.issueType.isEmpty() ? QStringLiteral("Bug") : bug.issueType}}},
        {"summary", bug.title.trimmed()},
        {"description", bug.jiraDescription()},
    };
    QJsonArray labels{QStringLiteral("qaflow")};
    if (!bug.linkedCaseId.isEmpty()) labels.append(bug.linkedCaseId);
    for (const auto& l : bug.labels) if (!l.trimmed().isEmpty()) labels.append(l.trimmed().replace(QLatin1Char(' '), QLatin1Char('-')));
    fields["labels"] = labels;
    if (!bug.priority.isEmpty()) fields["priority"] = QJsonObject{{"name", bug.priority}};
    if (!bug.assigneeId.isEmpty())
        fields["assignee"] = s.usesAccountId() ? QJsonObject{{"accountId", bug.assigneeId}} : QJsonObject{{"name", bug.assigneeId}};
    if (!bug.components.isEmpty()) {
        QJsonArray comps;
        for (const auto& c : bug.components) if (!c.trimmed().isEmpty()) comps.append(QJsonObject{{"name", c.trimmed()}});
        if (!comps.isEmpty()) fields["components"] = comps;
    }
    if (!bug.affectsVersions.isEmpty()) {
        QJsonArray versions;
        for (const auto& v : bug.affectsVersions) if (!v.trimmed().isEmpty()) versions.append(QJsonObject{{"name", v.trimmed()}});
        if (!versions.isEmpty()) fields["versions"] = versions;
    }

    postJson(request(s, QStringLiteral("/rest/api/2/issue")), QJsonDocument(QJsonObject{{"fields", fields}}), [this, s, bug, done](const Response& r) {
        if (!r.ok) { IssueResult f; f.error = errorFor(s, r); f.retryable = r.retryable; done(f); return; }
        IssueResult res;
        res.ok = true;
        res.key = r.json.object()[QStringLiteral("key")].toString();
        res.url = s.issueUrl(res.key);
        uploadAttachments(s, res, existingFiles(bug.attachmentPaths), done);
    });
}

void JiraClient::uploadAttachments(const TrackerSettings& s, IssueResult result, QStringList pending, std::function<void(const IssueResult&)> done) {
    if (pending.isEmpty()) { done(result); return; }
    const QString path = pending.takeFirst();
    QHttpMultiPart* multi = multipartFile(path);
    if (!multi) { uploadAttachments(s, result, pending, done); return; }
    QNetworkRequest req = request(s, QStringLiteral("/rest/api/2/issue/%1/attachments").arg(result.key));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant());   // lo fija el multipart
    req.setRawHeader("X-Atlassian-Token", "no-check");
    postMultipart(req, multi, [this, s, result, pending, done](const Response& r) mutable {
        if (r.ok) ++result.attachmentsUploaded;
        uploadAttachments(s, result, pending, done);
    });
}

void JiraClient::fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) {
    get(request(s, QStringLiteral("/rest/api/2/issue/%1?fields=status").arg(key)), [s, done](const Response& r) {
        if (!r.ok) { done(IssueStatus{false, {}, false, errorFor(s, r)}); return; }
        const QJsonObject status = r.json.object()[QStringLiteral("fields")].toObject()[QStringLiteral("status")].toObject();
        IssueStatus st;
        st.ok = true;
        st.status = status[QStringLiteral("name")].toString();
        st.resolved = status[QStringLiteral("statusCategory")].toObject()[QStringLiteral("key")].toString() == QStringLiteral("done");
        done(st);
    });
}

void JiraClient::fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) {
    const QString key = s.project.trimmed();
    // Tres peticiones encadenadas: proyecto (tipos, componentes, versiones), prioridades y asignables.
    get(request(s, QStringLiteral("/rest/api/2/project/%1").arg(key)), [this, s, key, done](const Response& r) {
        if (!r.ok) { done(MetadataResult{false, {}, errorFor(s, r)}); return; }
        ProjectMetadata meta;
        const QJsonObject p = r.json.object();
        for (const auto& v : p[QStringLiteral("issueTypes")].toArray()) {
            const QJsonObject t = v.toObject();
            if (!t[QStringLiteral("subtask")].toBool()) meta.issueTypes << t[QStringLiteral("name")].toString();
        }
        for (const auto& v : p[QStringLiteral("components")].toArray()) meta.components << v.toObject()[QStringLiteral("name")].toString();
        for (const auto& v : p[QStringLiteral("versions")].toArray()) {
            const QJsonObject ver = v.toObject();
            if (!ver[QStringLiteral("archived")].toBool()) meta.versions << ver[QStringLiteral("name")].toString();
        }
        get(request(s, QStringLiteral("/rest/api/2/priority")), [this, s, key, meta, done](const Response& r2) mutable {
            if (r2.ok) for (const auto& v : r2.json.array()) meta.priorities << v.toObject()[QStringLiteral("name")].toString();
            const bool accountIds = s.usesAccountId();
            // Primeros asignables del proyecto: el formulario arranca con ellos y luego busca en el servidor.
            get(request(s, assignableSearchPath(s, QString(), 100)), [meta, accountIds, done](const Response& r3) mutable {
                if (r3.ok) meta.assignees = assigneesFrom(r3.json.array(), accountIds);
                done(MetadataResult{true, meta, {}});
            });
        });
    });
}

} // namespace qaflow
