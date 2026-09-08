#pragma once

#include "infrastructure/tracker/HttpTrackerClient.h"

namespace qaflow {

/// Azure DevOps Work Items (REST 7.0). `url` es https://dev.azure.com/organizacion (o el servidor
/// propio con colección) y `project` el nombre del proyecto. Autenticación Basic con PAT.
class AzureDevOpsClient : public HttpTrackerClient {
    Q_OBJECT
public:
    explicit AzureDevOpsClient(QObject* parent = nullptr) : HttpTrackerClient(parent) {}

    void testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) override;
    void createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) override;
    void fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) override;
    void fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) override;

private:
    QNetworkRequest request(const TrackerSettings& s, const QString& path, const QString& contentType = QStringLiteral("application/json")) const;
    void attachNext(const TrackerSettings& s, IssueResult result, QStringList pending, std::function<void(const IssueResult&)> done);
};

} // namespace qaflow
