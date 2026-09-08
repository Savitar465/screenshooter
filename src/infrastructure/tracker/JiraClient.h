#pragma once

#include "infrastructure/tracker/HttpTrackerClient.h"

namespace qaflow {

/// Cliente REST de Jira (API v2). Autenticación:
///  - con `email` configurado → Basic (email:token), lo que requiere Jira Cloud;
///  - sin email → Bearer token (PAT de Jira Server/Data Center).
class JiraClient : public HttpTrackerClient {
    Q_OBJECT
public:
    explicit JiraClient(QObject* parent = nullptr) : HttpTrackerClient(parent) {}

    void testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) override;
    void createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) override;
    void fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) override;
    void fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) override;

private:
    QNetworkRequest request(const TrackerSettings& s, const QString& path) const;
    void uploadAttachments(const TrackerSettings& s, IssueResult result, QStringList pending, std::function<void(const IssueResult&)> done);
};

} // namespace qaflow
