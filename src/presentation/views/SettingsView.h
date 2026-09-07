#pragma once

#include <QWidget>

class QLineEdit;
class QComboBox;
class QPushButton;

namespace qaflow {

class SettingsStore;
class BugReportService;

/// Pantalla "Ajustes": integración Jira y preferencias de captura.
class SettingsView : public QWidget {
    Q_OBJECT
public:
    SettingsView(SettingsStore& settings, BugReportService& bugs, QWidget* parent = nullptr);

signals:
    void toast(const QString& message, const QString& color);

private:
    void refreshJira();
    void refreshCapture();
    void testConnection();

    SettingsStore& m_settings;
    BugReportService& m_bugs;
    bool m_selfEdit = false;

    QPushButton* m_badge;
    QLineEdit* m_url;
    QLineEdit* m_project;
    QLineEdit* m_email;
    QLineEdit* m_token;
    QLineEdit* m_shortcut;
    QComboBox* m_format;
    QComboBox* m_mode;
    QLineEdit* m_folder;
};

} // namespace qaflow
