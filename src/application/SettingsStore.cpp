#include "SettingsStore.h"

#include <QDir>
#include <QStandardPaths>

namespace qaflow {

SettingsStore::SettingsStore(std::shared_ptr<ISettingsRepository> repo, QObject* parent)
    : QObject(parent), m_repo(std::move(repo)) {}

void SettingsStore::load() {
    if (m_repo) {
        m_jira = m_repo->loadJira();
        m_capture = m_repo->loadCapture();
    }
    if (m_capture.folder.isEmpty())
        m_capture.folder = QDir(QStandardPaths::writableLocation(QStandardPaths::HomeLocation)).filePath(QStringLiteral("QAflow/capturas"));
    emit jiraChanged();
    emit captureChanged();
}

void SettingsStore::updateJira(const std::function<void(JiraSettings&)>& mutate) {
    mutate(m_jira);
    if (m_repo) m_repo->saveJira(m_jira);
    emit jiraChanged();
}

void SettingsStore::updateCapture(const std::function<void(CaptureSettings&)>& mutate) {
    mutate(m_capture);
    if (m_repo) m_repo->saveCapture(m_capture);
    emit captureChanged();
}

} // namespace qaflow
