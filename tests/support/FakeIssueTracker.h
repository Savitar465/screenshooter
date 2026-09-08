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
};

} // namespace qaflow::testing
