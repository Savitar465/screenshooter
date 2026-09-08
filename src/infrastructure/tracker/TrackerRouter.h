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

private:
    IIssueTracker& client(TrackerKind kind);

    std::map<TrackerKind, std::unique_ptr<IIssueTracker>> m_clients;
};

} // namespace qaflow
