#include "TrackerRouter.h"

#include "infrastructure/tracker/AzureDevOpsClient.h"
#include "infrastructure/tracker/GitHubClient.h"
#include "infrastructure/tracker/GitLabClient.h"
#include "infrastructure/tracker/JiraClient.h"

namespace qaflow {

TrackerRouter::TrackerRouter(QObject* parent) : QObject(parent) {
    m_clients[TrackerKind::Jira] = std::make_unique<JiraClient>();
    m_clients[TrackerKind::GitHub] = std::make_unique<GitHubClient>();
    m_clients[TrackerKind::GitLab] = std::make_unique<GitLabClient>();
    m_clients[TrackerKind::AzureDevOps] = std::make_unique<AzureDevOpsClient>();
}

IIssueTracker& TrackerRouter::client(TrackerKind kind) {
    auto it = m_clients.find(kind);
    return it == m_clients.end() ? *m_clients[TrackerKind::Jira] : *it->second;
}

const IIssueTracker& TrackerRouter::client(TrackerKind kind) const {
    const auto it = m_clients.find(kind);
    return it == m_clients.end() ? *m_clients.at(TrackerKind::Jira) : *it->second;
}

void TrackerRouter::testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) { client(s.kind).testConnection(s, std::move(done)); }
void TrackerRouter::createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) { client(s.kind).createIssue(s, bug, std::move(done)); }
void TrackerRouter::fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) { client(s.kind).fetchStatus(s, key, std::move(done)); }
void TrackerRouter::fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) { client(s.kind).fetchMetadata(s, std::move(done)); }
void TrackerRouter::searchAssignees(const TrackerSettings& s, const QString& query, std::function<void(const AssigneeSearch&)> done) { client(s.kind).searchAssignees(s, query, std::move(done)); }
bool TrackerRouter::canSearchAssignees(const TrackerSettings& s) const { return client(s.kind).canSearchAssignees(s); }

} // namespace qaflow
