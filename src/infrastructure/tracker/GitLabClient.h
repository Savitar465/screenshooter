#pragma once

#include "infrastructure/tracker/HttpTrackerClient.h"

namespace qaflow {

/// GitLab Issues (API v4). `url` es la instancia (https://gitlab.com) y `project` la ruta
/// "grupo/proyecto" o el id numérico. Las capturas se suben con /uploads y se enlazan en la descripción.
class GitLabClient : public HttpTrackerClient {
    Q_OBJECT
public:
    explicit GitLabClient(QObject* parent = nullptr) : HttpTrackerClient(parent) {}

    void testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) override;
    void createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) override;
    void fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) override;
    void fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) override;

private:
    QNetworkRequest request(const TrackerSettings& s, const QString& path) const;
    static QString projectPath(const TrackerSettings& s);
    void uploadThenCreate(const TrackerSettings& s, const BugReport& bug, QStringList pending, QStringList links, int uploaded,
                          std::function<void(const IssueResult&)> done);
};

} // namespace qaflow
