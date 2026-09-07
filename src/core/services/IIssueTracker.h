#pragma once

#include "core/models/BugReport.h"
#include "core/models/Settings.h"

#include <QString>
#include <functional>

namespace qaflow {

struct IssueResult {
    bool ok = false;
    QString key;        // SHOP-143
    QString url;
    QString error;
    int attachmentsUploaded = 0;
};

struct ConnectionResult {
    bool ok = false;
    QString displayName;
    QString error;
};

/// Gestor de incidencias (Jira). Asíncrono: las llamadas devuelven por callback.
class IIssueTracker {
public:
    virtual ~IIssueTracker() = default;
    virtual void testConnection(const JiraSettings& s, std::function<void(const ConnectionResult&)> done) = 0;
    virtual void createIssue(const JiraSettings& s, const BugReport& bug, std::function<void(const IssueResult&)> done) = 0;
};

} // namespace qaflow
