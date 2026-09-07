#pragma once

#include "core/services/IIssueTracker.h"

#include <QNetworkAccessManager>
#include <QObject>

class QNetworkRequest;
class QNetworkReply;

namespace qaflow {

/// Cliente REST de Jira (API v2). Autenticación:
///  - con `email` configurado → Basic (email:token), lo que requiere Jira Cloud;
///  - sin email → Bearer token (PAT de Jira Server/Data Center).
class JiraClient : public QObject, public IIssueTracker {
    Q_OBJECT
public:
    explicit JiraClient(QObject* parent = nullptr);

    void testConnection(const JiraSettings& s, std::function<void(const ConnectionResult&)> done) override;
    void createIssue(const JiraSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) override;

private:
    QNetworkRequest request(const JiraSettings& s, const QString& path) const;
    void uploadAttachments(const JiraSettings& s, IssueResult result, QStringList pending, std::function<void(const IssueResult&)> done);
    static QString errorFrom(QNetworkReply* reply);

    QNetworkAccessManager m_nam;
};

} // namespace qaflow
