#pragma once

#include "core/services/IIssueTracker.h"

#include <QObject>
#include <map>
#include <memory>

namespace qaflow {

/// Despacha cada llamada al cliente del gestor configurado (`TrackerSettings::kind`).
/// La capa de aplicación sólo ve un IIssueTracker.
class TrackerRouter : public QObject, public IIssueTracker {
    Q_OBJECT
public:
    explicit TrackerRouter(QObject* parent = nullptr);

    void testConnection(const TrackerSettings& s, std::function<void(const ConnectionResult&)> done) override;
    void createIssue(const TrackerSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) override;
    void fetchStatus(const TrackerSettings& s, const QString& key, std::function<void(const IssueStatus&)> done) override;
    void fetchMetadata(const TrackerSettings& s, std::function<void(const MetadataResult&)> done) override;
    bool canSearchAssignees(const TrackerSettings& s) const override;
    void searchAssignees(const TrackerSettings& s, const QString& query, std::function<void(const AssigneeSearch&)> done) override;

private:
    IIssueTracker& client(TrackerKind kind);
    /// Igual que `client()`, para las consultas const del gestor configurado.
    const IIssueTracker& client(TrackerKind kind) const;

    std::map<TrackerKind, std::unique_ptr<IIssueTracker>> m_clients;
};

} // namespace qaflow
