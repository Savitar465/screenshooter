#pragma once

#include <QWidget>

class QLineEdit;
class QComboBox;
class QPushButton;
class QLabel;
class QCheckBox;

namespace qaflow {

class SettingsStore;
class BugReportService;

/// Pantalla "Ajustes": preferencias generales (idioma, tema, bandeja), gestor de incidencias
/// (Jira, GitHub, GitLab, Azure DevOps) y preferencias de captura.
class SettingsView : public QWidget {
    Q_OBJECT
public:
    SettingsView(SettingsStore& settings, BugReportService& bugs, QWidget* parent = nullptr);

signals:
    void toast(const QString& message, const QString& color);

private:
    void refreshGeneral();
    void refreshTracker();
    void refreshCapture();
    void testConnection();

    SettingsStore& m_settings;
    BugReportService& m_bugs;
    bool m_selfEdit = false;

    QComboBox* m_language;
    QComboBox* m_theme;
    QCheckBox* m_closeToTray;
    QPushButton* m_badge;
    QComboBox* m_kind;
    QLabel* m_kindHint;
    QLineEdit* m_url;
    QLabel* m_projectLabel;
    QLineEdit* m_project;
    QWidget* m_emailField;
    QLineEdit* m_email;
    QLineEdit* m_token;
    QLabel* m_secretNote;
    QLineEdit* m_shortcut;
    QComboBox* m_format;
    QComboBox* m_mode;
    QLineEdit* m_folder;
};

} // namespace qaflow
