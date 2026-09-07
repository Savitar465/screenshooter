#pragma once

#include "core/models/BugReport.h"
#include "core/services/IIssueTracker.h"

#include <QObject>
#include <memory>

namespace qaflow {

class TestCaseStore;
class RunController;
class SettingsStore;

/// Construye el borrador de bug a partir de la ejecución y lo envía a Jira.
class BugReportService : public QObject {
    Q_OBJECT
public:
    BugReportService(std::shared_ptr<IIssueTracker> tracker, TestCaseStore& cases, RunController& run,
                     SettingsStore& settings, QObject* parent = nullptr);

    /// Borrador prellenado con el caso seleccionado y, si existe, el primer paso fallido de la ejecución.
    BugReport draftFromCurrentContext() const;

    void submit(const BugReport& bug, std::function<void(const IssueResult&)> done);
    void testConnection(std::function<void(const ConnectionResult&)> done);

private:
    std::shared_ptr<IIssueTracker> m_tracker;
    TestCaseStore& m_cases;
    RunController& m_run;
    SettingsStore& m_settings;
};

} // namespace qaflow
