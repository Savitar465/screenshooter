#include "JiraClient.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

namespace qaflow {

JiraClient::JiraClient(QObject* parent) : QObject(parent) {}

QNetworkRequest JiraClient::request(const JiraSettings& s, const QString& path) const {
    QString base = s.url.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    QNetworkRequest req(QUrl(base + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Accept", "application/json");
    if (!s.email.trimmed().isEmpty())
        req.setRawHeader("Authorization", "Basic " + (s.email.trimmed() + QLatin1Char(':') + s.token).toUtf8().toBase64());
    else
        req.setRawHeader("Authorization", "Bearer " + s.token.toUtf8());
    req.setTransferTimeout(15000);
    return req;
}

QString JiraClient::errorFrom(QNetworkReply* reply) {
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto body = QJsonDocument::fromJson(reply->readAll());
    QStringList msgs;
    if (body.isObject()) {
        for (const auto& v : body.object()[QStringLiteral("errorMessages")].toArray()) msgs << v.toString();
        const auto errs = body.object()[QStringLiteral("errors")].toObject();
        for (auto it = errs.begin(); it != errs.end(); ++it) msgs << it.key() + QStringLiteral(": ") + it.value().toString();
    }
    if (msgs.isEmpty()) msgs << reply->errorString();
    return code ? QStringLiteral("HTTP %1 · %2").arg(code).arg(msgs.join(QStringLiteral("; "))) : msgs.join(QStringLiteral("; "));
}

void JiraClient::testConnection(const JiraSettings& s, std::function<void(const ConnectionResult&)> done) {
    if (s.url.trimmed().isEmpty() || s.token.trimmed().isEmpty()) {
        done(ConnectionResult{false, {}, QStringLiteral("Indica la URL y el token de API")});
        return;
    }
    QNetworkReply* reply = m_nam.get(request(s, QStringLiteral("/rest/api/2/myself")));
    connect(reply, &QNetworkReply::finished, this, [reply, done]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { done(ConnectionResult{false, {}, errorFrom(reply)}); return; }
        const auto o = QJsonDocument::fromJson(reply->readAll()).object();
        done(ConnectionResult{true, o[QStringLiteral("displayName")].toString(), {}});
    });
}

void JiraClient::createIssue(const JiraSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) {
    const QJsonObject fields{
        {"project", QJsonObject{{"key", s.project.trimmed()}}},
        {"issuetype", QJsonObject{{"name", "Bug"}}},
        {"summary", bug.title.trimmed()},
        {"description", bug.jiraDescription()},
        {"labels", QJsonArray{QStringLiteral("qaflow"), bug.linkedCaseId}},
    };
    QNetworkReply* reply = m_nam.post(request(s, QStringLiteral("/rest/api/2/issue")), QJsonDocument(QJsonObject{{"fields", fields}}).toJson());
    connect(reply, &QNetworkReply::finished, this, [this, s, bug, reply, done]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) { done(IssueResult{false, {}, {}, errorFrom(reply)}); return; }
        const auto o = QJsonDocument::fromJson(reply->readAll()).object();
        IssueResult r;
        r.ok = true;
        r.key = o[QStringLiteral("key")].toString();
        QString base = s.url.trimmed();
        while (base.endsWith(QLatin1Char('/'))) base.chop(1);
        r.url = base + QStringLiteral("/browse/") + r.key;
        uploadAttachments(s, r, bug.attachmentPaths, done);
    });
}

void JiraClient::uploadAttachments(const JiraSettings& s, IssueResult result, QStringList pending, std::function<void(const IssueResult&)> done) {
    while (!pending.isEmpty() && !QFileInfo::exists(pending.first())) pending.removeFirst();
    if (pending.isEmpty()) { done(result); return; }

    const QString path = pending.takeFirst();
    auto* file = new QFile(path);
    if (!file->open(QIODevice::ReadOnly)) { delete file; uploadAttachments(s, result, pending, done); return; }

    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader, QMimeDatabase().mimeTypeForFile(path).name());
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
                   QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(QFileInfo(path).fileName()));
    part.setBodyDevice(file);
    file->setParent(multi);
    multi->append(part);

    QNetworkRequest req = request(s, QStringLiteral("/rest/api/2/issue/%1/attachments").arg(result.key));
    req.setHeader(QNetworkRequest::ContentTypeHeader, QVariant()); // lo fija el multipart
    req.setRawHeader("X-Atlassian-Token", "no-check");
    QNetworkReply* reply = m_nam.post(req, multi);
    multi->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, s, result, pending, reply, done]() mutable {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) ++result.attachmentsUploaded;
        uploadAttachments(s, result, pending, done);
    });
}

} // namespace qaflow
