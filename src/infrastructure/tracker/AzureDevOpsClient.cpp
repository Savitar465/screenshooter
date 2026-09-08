#include "AzureDevOpsClient.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

namespace qaflow {

namespace {
const QString kApi = QStringLiteral("api-version=7.0");

QJsonObject op(const QString& path, const QJsonValue& value) {
    return QJsonObject{{"op", "add"}, {"path", path}, {"value", value}};
}
} // namespace

QNetworkRequest AzureDevOpsClient::request(const TrackerSettings& s, const QString& path, const QString& contentType) const {
    QNetworkRequest req = jsonRequest(s.baseUrl() + path);
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    req.setRawHeader("Authorization", "Basic " + (QByteArray(":") + s.token.toUtf8()).toBase64());
    return req;
}

void AzureDevOpsClient::testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) {
    if (s.url.trimmed().isEmpty() || s.token.trimmed().isEmpty()) {
        done(ConnectionResult{false, {}, QCoreApplication::translate("infrastructure", "Indica la URL de la organización y el PAT")});
        return;
    }
    get(request(s, QStringLiteral("/_apis/connectionData?") + kApi), [done](const Response& r) {
        if (!r.ok) { done(ConnectionResult{false, {}, r.error}); return; }
        const QJsonObject user = r.json.object()[QStringLiteral("authenticatedUser")].toObject();
        done(ConnectionResult{true, user[QStringLiteral("providerDisplayName")].toString(), {}});
    });
}

void AzureDevOpsClient::createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) {
    QJsonArray patch;
    patch.append(op(QStringLiteral("/fields/System.Title"), bug.title.trimmed()));
    patch.append(op(QStringLiteral("/fields/System.Description"), bug.htmlDescription()));
    patch.append(op(QStringLiteral("/fields/Microsoft.VSTS.TCM.ReproSteps"), bug.htmlDescription()));
    QStringList tags{QStringLiteral("qaflow")};
    if (!bug.linkedCaseId.isEmpty()) tags << bug.linkedCaseId;
    for (const auto& t : bug.components + bug.labels) if (!t.trimmed().isEmpty()) tags << t.trimmed();
    patch.append(op(QStringLiteral("/fields/System.Tags"), tags.join(QStringLiteral("; "))));
    bool isNumber = false;
    const int prio = bug.priority.toInt(&isNumber);
    if (isNumber && prio >= 1 && prio <= 4) patch.append(op(QStringLiteral("/fields/Microsoft.VSTS.Common.Priority"), prio));
    if (!bug.assigneeId.isEmpty()) patch.append(op(QStringLiteral("/fields/System.AssignedTo"), bug.assigneeId));
    if (!bug.affectsVersions.isEmpty()) patch.append(op(QStringLiteral("/fields/Microsoft.VSTS.Build.FoundIn"), bug.affectsVersions.join(QStringLiteral(", "))));

    const QString type = bug.issueType.isEmpty() ? QStringLiteral("Bug") : bug.issueType;
    const QString path = QStringLiteral("/%1/_apis/wit/workitems/$%2?%3").arg(QString::fromUtf8(QUrl::toPercentEncoding(s.project.trimmed())), QString::fromUtf8(QUrl::toPercentEncoding(type)), kApi);
    sendCustom("POST", request(s, path, QStringLiteral("application/json-patch+json")), QJsonDocument(patch).toJson(QJsonDocument::Compact),
               [this, s, bug, done](const Response& r) {
        if (!r.ok) { IssueResult f; f.error = r.error; f.retryable = r.retryable; done(f); return; }
        IssueResult res;
        res.ok = true;
        res.key = QString::number(r.json.object()[QStringLiteral("id")].toInt());
        res.url = r.json.object()[QStringLiteral("_links")].toObject()[QStringLiteral("html")].toObject()[QStringLiteral("href")].toString();
        if (res.url.isEmpty()) res.url = s.issueUrl(res.key);
        attachNext(s, res, existingFiles(bug.attachmentPaths), done);
    });
}

void AzureDevOpsClient::attachNext(const TrackerSettings& s, IssueResult result, QStringList pending, std::function<void(const IssueResult&)> done) {
    if (pending.isEmpty()) { done(result); return; }
    const QString path = pending.takeFirst();
    const QString name = QString::fromUtf8(QUrl::toPercentEncoding(QFileInfo(path).fileName()));
    const QString project = QString::fromUtf8(QUrl::toPercentEncoding(s.project.trimmed()));
    // 1) subir el fichero; 2) enlazarlo al work item como relación AttachedFile.
    postFile(request(s, QStringLiteral("/%1/_apis/wit/attachments?fileName=%2&%3").arg(project, name, kApi)), path,
             [this, s, result, pending, done](const Response& r) mutable {
        if (!r.ok) { attachNext(s, result, pending, done); return; }
        const QString url = r.json.object()[QStringLiteral("url")].toString();
        QJsonArray patch;
        patch.append(op(QStringLiteral("/relations/-"), QJsonObject{{"rel", "AttachedFile"}, {"url", url}, {"attributes", QJsonObject{{"comment", "Captura QAflow"}}}}));
        sendCustom("PATCH", request(s, QStringLiteral("/_apis/wit/workitems/%1?%2").arg(result.key, kApi), QStringLiteral("application/json-patch+json")),
                   QJsonDocument(patch).toJson(QJsonDocument::Compact), [this, s, result, pending, done](const Response& r2) mutable {
            if (r2.ok) ++result.attachmentsUploaded;
            attachNext(s, result, pending, done);
        });
    });
}

void AzureDevOpsClient::fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) {
    get(request(s, QStringLiteral("/_apis/wit/workitems/%1?fields=System.State&%2").arg(key, kApi)), [done](const Response& r) {
        if (!r.ok) { done(IssueStatus{false, {}, false, r.error}); return; }
        const QString state = r.json.object()[QStringLiteral("fields")].toObject()[QStringLiteral("System.State")].toString();
        static const QStringList closed{QStringLiteral("Closed"), QStringLiteral("Done"), QStringLiteral("Resolved"), QStringLiteral("Removed"), QStringLiteral("Completed")};
        done(IssueStatus{true, state, closed.contains(state, Qt::CaseInsensitive), {}});
    });
}

void AzureDevOpsClient::fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) {
    const QString project = QString::fromUtf8(QUrl::toPercentEncoding(s.project.trimmed()));
    get(request(s, QStringLiteral("/%1/_apis/wit/workitemtypes?%2").arg(project, kApi)), [done](const Response& r) {
        if (!r.ok) { done(MetadataResult{false, {}, r.error}); return; }
        ProjectMetadata meta;
        for (const auto& v : r.json.object()[QStringLiteral("value")].toArray()) meta.issueTypes << v.toObject()[QStringLiteral("name")].toString();
        meta.priorities = {QStringLiteral("1"), QStringLiteral("2"), QStringLiteral("3"), QStringLiteral("4")};
        done(MetadataResult{true, meta, {}});
    });
}

} // namespace qaflow
