#include "GitHubClient.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

namespace qaflow {

QNetworkRequest GitHubClient::request(const TrackerSettings& s, const QString& path) const {
    QNetworkRequest req = jsonRequest(s.baseUrl() + path);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("Authorization", "Bearer " + s.token.toUtf8());
    req.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    return req;
}

void GitHubClient::testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) {
    if (s.url.trimmed().isEmpty() || s.token.trimmed().isEmpty()) {
        done(ConnectionResult{false, {}, QCoreApplication::translate("infrastructure", "Indica la URL de la API y el token")});
        return;
    }
    get(request(s, QStringLiteral("/user")), [done](const Response& r) {
        if (!r.ok) { done(ConnectionResult{false, {}, r.error}); return; }
        const QJsonObject u = r.json.object();
        const QString name = u[QStringLiteral("name")].toString();
        done(ConnectionResult{true, name.isEmpty() ? u[QStringLiteral("login")].toString() : name, {}});
    });
}

void GitHubClient::createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) {
    QStringList files;
    for (const auto& p : existingFiles(bug.attachmentPaths)) files << QCoreApplication::translate("infrastructure", "- %1 (adjunto local, no subido: la API de GitHub no admite ficheros)").arg(QFileInfo(p).fileName());
    QJsonArray labels{QStringLiteral("qaflow")};
    if (!bug.linkedCaseId.isEmpty()) labels.append(bug.linkedCaseId);
    for (const auto& l : bug.components + bug.labels) if (!l.trimmed().isEmpty()) labels.append(l.trimmed());
    if (!bug.priority.isEmpty()) labels.append(bug.priority);
    QJsonObject body{
        {"title", bug.title.trimmed()},
        {"body", bug.markdownDescription(files)},
        {"labels", labels},
    };
    if (!bug.assigneeId.isEmpty()) body["assignees"] = QJsonArray{bug.assigneeId};

    postJson(request(s, QStringLiteral("/repos/%1/issues").arg(s.project.trimmed())), QJsonDocument(body), [s, done](const Response& r) {
        if (!r.ok) { IssueResult f; f.error = r.error; f.retryable = r.retryable; done(f); return; }
        IssueResult res;
        res.ok = true;
        res.key = QStringLiteral("#%1").arg(r.json.object()[QStringLiteral("number")].toInt());
        res.url = r.json.object()[QStringLiteral("html_url")].toString();
        if (res.url.isEmpty()) res.url = s.issueUrl(res.key);
        done(res);
    });
}

void GitHubClient::fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) {
    const QString number = QString(key).remove(QLatin1Char('#'));
    get(request(s, QStringLiteral("/repos/%1/issues/%2").arg(s.project.trimmed(), number)), [done](const Response& r) {
        if (!r.ok) { done(IssueStatus{false, {}, false, r.error}); return; }
        const QString state = r.json.object()[QStringLiteral("state")].toString();
        done(IssueStatus{true, state, state == QStringLiteral("closed"), {}});
    });
}

void GitHubClient::fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) {
    const QString repo = s.project.trimmed();
    get(request(s, QStringLiteral("/repos/%1/labels?per_page=100").arg(repo)), [this, s, repo, done](const Response& r) {
        if (!r.ok) { done(MetadataResult{false, {}, r.error}); return; }
        ProjectMetadata meta;
        meta.issueTypes = {QStringLiteral("Issue")};
        for (const auto& v : r.json.array()) meta.components << v.toObject()[QStringLiteral("name")].toString();
        get(request(s, QStringLiteral("/repos/%1/assignees?per_page=100").arg(repo)), [this, s, repo, meta, done](const Response& r2) mutable {
            if (r2.ok) for (const auto& v : r2.json.array()) {
                const QString login = v.toObject()[QStringLiteral("login")].toString();
                meta.assignees.append(Assignee{login, login});
            }
            get(request(s, QStringLiteral("/repos/%1/milestones?state=open&per_page=100").arg(repo)), [meta, done](const Response& r3) mutable {
                if (r3.ok) for (const auto& v : r3.json.array()) meta.versions << v.toObject()[QStringLiteral("title")].toString();
                done(MetadataResult{true, meta, {}});
            });
        });
    });
}

} // namespace qaflow
