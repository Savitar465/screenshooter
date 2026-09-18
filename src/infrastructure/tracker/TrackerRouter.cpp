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
bool TrackerRouter::canListProjects(const TrackerSettings& s) const { return client(s.kind).canListProjects(s); }
void TrackerRouter::fetchProjects(const TrackerSettings& s, std::function<void(const TrackerProjectList&)> done) { client(s.kind).fetchProjects(s, std::move(done)); }
bool TrackerRouter::canPublishIssues(const TrackerSettings& s) const { return client(s.kind).canPublishIssues(s); }
void TrackerRouter::publishIssue(const TrackerSettings& s, const TrackerIssueDraft& draft, std::function<void(const IssueResult&)> done) { client(s.kind).publishIssue(s, draft, std::move(done)); }
void TrackerRouter::fetchIssue(const TrackerSettings& s, const QString& key, std::function<void(const TrackerIssueInfo&)> done) { client(s.kind).fetchIssue(s, key, std::move(done)); }
void TrackerRouter::updateIssue(const TrackerSettings& s, const QString& key, const TrackerIssueDraft& draft, std::function<void(const IssueResult&)> done) { client(s.kind).updateIssue(s, key, draft, std::move(done)); }
bool TrackerRouter::canSearchIssues(const TrackerSettings& s) const { return client(s.kind).canSearchIssues(s); }
void TrackerRouter::searchProjectBugs(const TrackerSettings& s, int startAt, int max, std::function<void(const TrackerIssueList&)> done) { client(s.kind).searchProjectBugs(s, startAt, max, std::move(done)); }
bool TrackerRouter::canCommentIssues(const TrackerSettings& s) const { return client(s.kind).canCommentIssues(s); }
void TrackerRouter::commentIssue(const TrackerSettings& s, const QString& key, const QString& body, const QStringList& attachments,
                                 std::function<void(const IssueResult&)> done) { client(s.kind).commentIssue(s, key, body, attachments, std::move(done)); }
bool TrackerRouter::canLinkIssues(const TrackerSettings& s) const { return client(s.kind).canLinkIssues(s); }
void TrackerRouter::linkIssues(const TrackerSettings& s, const QString& from, const QString& to, std::function<void(const IssueResult&)> done) { client(s.kind).linkIssues(s, from, to, std::move(done)); }

} // namespace qaflow
