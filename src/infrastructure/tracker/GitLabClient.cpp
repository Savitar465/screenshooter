#include "GitLabClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

namespace qaflow {

QString GitLabClient::projectPath(const TrackerSettings& s) {
    return QString::fromUtf8(QUrl::toPercentEncoding(s.project.trimmed()));
}

QNetworkRequest GitLabClient::request(const TrackerSettings& s, const QString& path) const {
    QNetworkRequest req = jsonRequest(s.baseUrl() + QStringLiteral("/api/v4") + path);
    req.setRawHeader("PRIVATE-TOKEN", s.token.toUtf8());
    return req;
}

void GitLabClient::testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) {
    if (s.url.trimmed().isEmpty() || s.token.trimmed().isEmpty()) {
        done(ConnectionResult{false, {}, QCoreApplication::translate("infrastructure", "Indica la URL de GitLab y el token")});
        return;
    }
    get(request(s, QStringLiteral("/user")), [done](const Response& r) {
        if (!r.ok) { done(ConnectionResult{false, {}, r.error}); return; }
        const QJsonObject u = r.json.object();
        const QString name = u[QStringLiteral("name")].toString();
        done(ConnectionResult{true, name.isEmpty() ? u[QStringLiteral("username")].toString() : name, {}});
    });
}

void GitLabClient::createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) {
    uploadThenCreate(s, bug, existingFiles(bug.attachmentPaths), {}, 0, std::move(done));
}

void GitLabClient::uploadThenCreate(const TrackerSettings& s, const BugReport& bug, QStringList pending, QStringList links, int uploaded,
                                    std::function<void(const IssueResult&)> done) {
    if (!pending.isEmpty()) {
        const QString path = pending.takeFirst();
        QHttpMultiPart* multi = multipartFile(path);
        if (!multi) { uploadThenCreate(s, bug, pending, links, uploaded, done); return; }
        QNetworkRequest req = request(s, QStringLiteral("/projects/%1/uploads").arg(projectPath(s)));
        req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant());
        postMultipart(req, multi, [this, s, bug, pending, links, uploaded, done](const Response& r) mutable {
            if (r.ok) {
                const QString md = r.json.object()[QStringLiteral("markdown")].toString();
                if (!md.isEmpty()) { links << md; ++uploaded; }
            }
            uploadThenCreate(s, bug, pending, links, uploaded, done);
        });
        return;
    }

    QStringList labels{QStringLiteral("qaflow")};
    if (!bug.linkedCaseId.isEmpty()) labels << bug.linkedCaseId;
    for (const auto& l : bug.components + bug.labels) if (!l.trimmed().isEmpty()) labels << l.trimmed();
    if (!bug.priority.isEmpty()) labels << bug.priority;
    QJsonObject body{
        {"title", bug.title.trimmed()},
        {"description", bug.markdownDescription(links)},
        {"labels", labels.join(QLatin1Char(','))},
        {"issue_type", bug.issueType.compare(QStringLiteral("incident"), Qt::CaseInsensitive) == 0 ? QStringLiteral("incident") : QStringLiteral("issue")},
    };
    bool isNumber = false;
    const int assigneeId = bug.assigneeId.toInt(&isNumber);
    if (isNumber && assigneeId > 0) body["assignee_ids"] = QJsonArray{assigneeId};

    postJson(request(s, QStringLiteral("/projects/%1/issues").arg(projectPath(s))), QJsonDocument(body), [s, uploaded, done](const Response& r) {
        if (!r.ok) { IssueResult f; f.error = r.error; f.retryable = r.retryable; done(f); return; }
        IssueResult res;
        res.ok = true;
        res.key = QStringLiteral("#%1").arg(r.json.object()[QStringLiteral("iid")].toInt());
        res.url = r.json.object()[QStringLiteral("web_url")].toString();
        if (res.url.isEmpty()) res.url = s.issueUrl(res.key);
        res.attachmentsUploaded = uploaded;
        done(res);
    });
}

void GitLabClient::fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) {
    const QString iid = QString(key).remove(QLatin1Char('#'));
    get(request(s, QStringLiteral("/projects/%1/issues/%2").arg(projectPath(s), iid)), [done](const Response& r) {
        if (!r.ok) { done(IssueStatus{false, {}, false, r.error}); return; }
        const QString state = r.json.object()[QStringLiteral("state")].toString();
        done(IssueStatus{true, state, state == QStringLiteral("closed"), {}});
    });
}

void GitLabClient::fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) {
    const QString p = projectPath(s);
    get(request(s, QStringLiteral("/projects/%1/labels?per_page=100").arg(p)), [this, s, p, done](const Response& r) {
        if (!r.ok) { done(MetadataResult{false, {}, r.error}); return; }
        ProjectMetadata meta;
        meta.issueTypes = {QStringLiteral("issue"), QStringLiteral("incident")};
        for (const auto& v : r.json.array()) meta.components << v.toObject()[QStringLiteral("name")].toString();
        get(request(s, QStringLiteral("/projects/%1/members/all?per_page=100").arg(p)), [this, s, p, meta, done](const Response& r2) mutable {
            if (r2.ok) for (const auto& v : r2.json.array()) {
                const QJsonObject u = v.toObject();
                meta.assignees.append(Assignee{QString::number(u[QStringLiteral("id")].toInt()), u[QStringLiteral("name")].toString()});
            }
            get(request(s, QStringLiteral("/projects/%1/milestones?state=active&per_page=100").arg(p)), [meta, done](const Response& r3) mutable {
                if (r3.ok) for (const auto& v : r3.json.array()) meta.versions << v.toObject()[QStringLiteral("title")].toString();
                done(MetadataResult{true, meta, {}});
            });
        });
    });
}

} // namespace qaflow
