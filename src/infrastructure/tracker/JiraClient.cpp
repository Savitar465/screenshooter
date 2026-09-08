#include "JiraClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

namespace qaflow {

QNetworkRequest JiraClient::request(const TrackerSettings& s, const QString& path) const {
    QNetworkRequest req = jsonRequest(s.baseUrl() + path);
    if (!s.email.trimmed().isEmpty())
        req.setRawHeader("Authorization", "Basic " + (s.email.trimmed() + QLatin1Char(':') + s.token).toUtf8().toBase64());
    else
        req.setRawHeader("Authorization", "Bearer " + s.token.toUtf8());
    return req;
}

void JiraClient::testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) {
    if (s.url.trimmed().isEmpty() || s.token.trimmed().isEmpty()) {
        done(ConnectionResult{false, {}, QCoreApplication::translate("infrastructure", "Indica la URL y el token de API")});
        return;
    }
    get(request(s, QStringLiteral("/rest/api/2/myself")), [done](const Response& r) {
        if (!r.ok) { done(ConnectionResult{false, {}, r.error}); return; }
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
        fields["assignee"] = s.email.trimmed().isEmpty() ? QJsonObject{{"name", bug.assigneeId}} : QJsonObject{{"accountId", bug.assigneeId}};
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
        if (!r.ok) { IssueResult f; f.error = r.error; f.retryable = r.retryable; done(f); return; }
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
    get(request(s, QStringLiteral("/rest/api/2/issue/%1?fields=status").arg(key)), [done](const Response& r) {
        if (!r.ok) { done(IssueStatus{false, {}, false, r.error}); return; }
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
        if (!r.ok) { done(MetadataResult{false, {}, r.error}); return; }
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
            const bool cloud = !s.email.trimmed().isEmpty();
            get(request(s, QStringLiteral("/rest/api/2/user/assignable/search?project=%1&maxResults=100").arg(key)), [meta, cloud, done](const Response& r3) mutable {
                if (r3.ok) {
                    for (const auto& v : r3.json.array()) {
                        const QJsonObject u = v.toObject();
                        Assignee a;
                        a.id = cloud ? u[QStringLiteral("accountId")].toString() : u[QStringLiteral("name")].toString();
                        a.name = u[QStringLiteral("displayName")].toString();
                        if (!a.id.isEmpty()) meta.assignees.append(a);
                    }
                }
                done(MetadataResult{true, meta, {}});
            });
        });
    });
}

} // namespace qaflow
