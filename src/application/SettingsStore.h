#pragma once

#include "core/models/Settings.h"
#include "core/services/ISettingsRepository.h"

#include <QObject>
#include <memory>

namespace qaflow {

class SettingsStore : public QObject {
    Q_OBJECT
public:
    explicit SettingsStore(std::shared_ptr<ISettingsRepository> repo, QObject* parent = nullptr);

    void load();

    const JiraSettings& jira() const { return m_jira; }
    const CaptureSettings& capture() const { return m_capture; }

    void updateJira(const std::function<void(JiraSettings&)>& mutate);
    void updateCapture(const std::function<void(CaptureSettings&)>& mutate);

signals:
    void jiraChanged();
    void captureChanged();

private:
    std::shared_ptr<ISettingsRepository> m_repo;
    JiraSettings m_jira;
    CaptureSettings m_capture;
};

} // namespace qaflow
