#pragma once

#include <QWidget>

class QLineEdit;
class QComboBox;
class QGridLayout;
class QPushButton;
class QLabel;
class QCheckBox;
class QSpinBox;

namespace qaflow {

class SettingsStore;
class BugReportService;
class IGlobalHotkey;

/// Pantalla "Ajustes": preferencias generales (idioma, tema, bandeja), gestor de incidencias
/// (Jira, GitHub, GitLab, Azure DevOps), preferencias de captura y atajos de la ejecución.
class SettingsView : public QWidget {
    Q_OBJECT
public:
    /// `hotkey` puede ser nullptr (tests); `captureBackend` es el texto informativo del método de captura.
    SettingsView(SettingsStore& settings, BugReportService& bugs, IGlobalHotkey* hotkey = nullptr,
                 const QString& captureBackend = QString(), QWidget* parent = nullptr);

signals:
    void toast(const QString& message, const QString& color);

private:
    void refreshGeneral();
    void refreshTracker();
    void refreshCapture();
    void refreshRunShortcuts();
    void refreshCaptureStatus();
    void testConnection();

    SettingsStore& m_settings;
    BugReportService& m_bugs;
    IGlobalHotkey* m_hotkey;
    QString m_captureBackend;
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
    QWidget* m_authField;
    QComboBox* m_jiraAuth;
    QGridLayout* m_projectGrid;
    QWidget* m_userField;
    QLabel* m_userLabel;
    QLineEdit* m_user;
    QGridLayout* m_credGrid;
    QLabel* m_tokenLabel;
    QLineEdit* m_token;
    QLabel* m_secretNote;
    QLineEdit* m_shortcut;
    QLineEdit* m_recordShortcut;
    QComboBox* m_format;
    QComboBox* m_mode;
    QComboBox* m_delay;
    QLineEdit* m_folder;
    QCheckBox* m_globalShortcut;
    QCheckBox* m_openEditor;
    QCheckBox* m_copyToClipboard;
    QSpinBox* m_gifFps;
    QSpinBox* m_gifMaxSecs;
    QLabel* m_captureStatus;
    QLineEdit* m_stepPass;
    QLineEdit* m_stepFail;
    QLineEdit* m_stepBack;
};

} // namespace qaflow
