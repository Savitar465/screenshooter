#pragma once

#include "infrastructure/tracker/HttpTrackerClient.h"

namespace qaflow {

/// GitHub Issues (REST v3). `url` es la base de la API (https://api.github.com o
/// https://host/api/v3 en Enterprise) y `project` el repositorio "owner/repo".
/// La API no admite adjuntos: las capturas se enumeran por nombre en el cuerpo.
class GitHubClient : public HttpTrackerClient {
    Q_OBJECT
public:
    explicit GitHubClient(QObject* parent = nullptr) : HttpTrackerClient(parent) {}

    void testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) override;
    void createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) override;
    void fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) override;
    void fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) override;

private:
    QNetworkRequest request(const TrackerSettings& s, const QString& path) const;
};

} // namespace qaflow
