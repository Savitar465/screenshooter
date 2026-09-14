#pragma once

// Gestor de incidencias falso: responde de inmediato según cómo se configure y guarda lo que
// recibió, para probar BugReportService sin red.

#include "core/services/IIssueTracker.h"

#include <QList>

namespace qaflow::testing {

class FakeIssueTracker : public IIssueTracker {
public:
    enum class Mode { Succeed, RejectContent, NetworkDown };
    Mode mode = Mode::Succeed;
    int nextNumber = 100;
    QList<BugReport> created;          // lo que se intentó crear, en orden
    QList<TrackerSettings> settingsSeen;
    QStringList statusQueries;
    QString statusToReturn = QStringLiteral("Done");
    bool resolvedToReturn = true;
    ProjectMetadata metadataToReturn;
    int metadataCalls = 0;
    bool searchesAssignees = false;         // como Jira, que busca personas en el servidor
    QList<Assignee> assigneesToReturn;
    QStringList assigneeQueries;            // lo que se buscó, en orden

    void testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) override {
        settingsSeen << s;
        if (mode == Mode::NetworkDown) done(ConnectionResult{false, {}, QStringLiteral("Host not found")});
        else done(ConnectionResult{true, QStringLiteral("QA Bot"), {}});
    }

    void createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) override {
        settingsSeen << s;
        created << bug;
        IssueResult r;
        switch (mode) {
            case Mode::Succeed:
                r.ok = true;
                r.key = QStringLiteral("%1-%2").arg(s.project).arg(nextNumber++);
                r.url = s.issueUrl(r.key);
                r.attachmentsUploaded = bug.attachmentPaths.size();
                break;
            case Mode::RejectContent:
                r.error = QStringLiteral("HTTP 400 · priority: Priority name 'Urgent' is not valid");
                r.retryable = false;
                break;
            case Mode::NetworkDown:
                r.error = QStringLiteral("Host not found");
                r.retryable = true;
                break;
        }
        done(r);
    }

    void fetchStatus(const TrackerSettings&, const QString& key, std::function<void(const IssueStatus&)> done) override {
        statusQueries << key;
        if (mode == Mode::NetworkDown) done(IssueStatus{false, {}, false, QStringLiteral("Host not found")});
        else done(IssueStatus{true, statusToReturn, resolvedToReturn, {}});
    }

    void fetchMetadata(const TrackerSettings&, std::function<void(const MetadataResult&)> done) override {
        ++metadataCalls;
        if (mode == Mode::NetworkDown) done(MetadataResult{false, {}, QStringLiteral("Host not found")});
        else done(MetadataResult{true, metadataToReturn, {}});
    }

    bool canSearchAssignees(const TrackerSettings&) const override { return searchesAssignees; }

    void searchAssignees(const TrackerSettings&, const QString& query, std::function<void(const AssigneeSearch&)> done) override {
        assigneeQueries << query;
        if (mode == Mode::NetworkDown) done(AssigneeSearch{false, {}, QStringLiteral("Host not found")});
        else done(AssigneeSearch{true, assigneesToReturn, {}});
    }

    bool listsProjects = true;              // como Jira, que lista los proyectos que ve el usuario
    QList<TrackerProject> projectsToReturn;
    int projectListCalls = 0;

    bool publishesIssues = true;                    // como Jira
    QList<TrackerIssueDraft> publishedIssues;       // lo que se mandó publicar, en orden
    QList<TrackerIssueDraft> updatedIssues;
    QStringList updatedKeys;
    TrackerIssueInfo issueToReturn;                 // lo que responde `fetchIssue`
    bool issueExists = true;

    bool canListProjects(const TrackerSettings&) const override { return listsProjects; }

    bool canPublishIssues(const TrackerSettings&) const override { return publishesIssues; }

    void publishIssue(const TrackerSettings& s, const TrackerIssueDraft& draft, std::function<void(const IssueResult&)> done) override {
        settingsSeen << s;
        publishedIssues << draft;
        IssueResult r;
        switch (mode) {
            case Mode::Succeed:
                r.ok = true;
                r.key = QStringLiteral("%1-%2").arg(s.project).arg(nextNumber++);
                r.url = s.issueUrl(r.key);
                break;
            case Mode::RejectContent:
                r.error = QStringLiteral("HTTP 400 · issuetype: valid issue type is required");
                break;
            case Mode::NetworkDown:
                r.error = QStringLiteral("Host not found");
                r.retryable = true;
                break;
        }
        done(r);
    }

    void fetchIssue(const TrackerSettings& s, const QString& key, std::function<void(const TrackerIssueInfo&)> done) override {
        if (mode == Mode::NetworkDown) {
            TrackerIssueInfo f;
            f.error = QStringLiteral("Host not found");
            f.retryable = true;
            done(f);
            return;
        }
        if (!issueExists) {
            TrackerIssueInfo f;
            f.error = QStringLiteral("%1 no existe").arg(key);
            done(f);
            return;
        }
        TrackerIssueInfo info = issueToReturn;
        info.ok = true;
        if (info.key.isEmpty()) info.key = key;
        if (info.url.isEmpty()) info.url = s.issueUrl(info.key);
        done(info);
    }

    void updateIssue(const TrackerSettings& s, const QString& key, const TrackerIssueDraft& draft, std::function<void(const IssueResult&)> done) override {
        updatedKeys << key;
        updatedIssues << draft;
        IssueResult r;
        if (mode == Mode::NetworkDown) { r.error = QStringLiteral("Host not found"); r.retryable = true; done(r); return; }
        if (mode == Mode::RejectContent) { r.error = QStringLiteral("HTTP 400 · summary is required"); done(r); return; }
        r.ok = true;
        r.key = key;
        r.url = s.issueUrl(key);
        done(r);
    }

    void fetchProjects(const TrackerSettings&, std::function<void(const TrackerProjectList&)> done) override {
        ++projectListCalls;
        if (mode == Mode::NetworkDown) done(TrackerProjectList{false, {}, QStringLiteral("Host not found")});
        else done(TrackerProjectList{true, projectsToReturn, {}});
    }
};

} // namespace qaflow::testing
