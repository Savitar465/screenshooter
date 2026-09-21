#pragma once

#include <QWidget>

class QLineEdit;
class QComboBox;
class QCompleter;
class QGridLayout;
class QPushButton;
class QLabel;
class QCheckBox;
class QSpinBox;

namespace qaflow {

struct AppContext;
class SettingsStore;
class BugReportService;
class TestPublishService;
class RequirementSourceService;
class ProjectStore;
class IGlobalHotkey;

/// Pantalla "Ajustes": configuración del proyecto abierto (código Jira y sistema de GESREQ), preferencias
/// generales (idioma, tema, bandeja), gestor de incidencias (Jira, GitHub, GitLab, Azure DevOps), conexión
/// con GESREQ, preferencias de captura y atajos de la ejecución.
class SettingsView : public QWidget {
    Q_OBJECT
public:
    /// Del contexto usa los ajustes y el servicio de bugs, que son obligatorios, y si los hay el atajo
    /// global, la publicación en Zephyr, la conexión con GESREQ y el catálogo de proyectos; sin ellos no se
    /// ofrece lo que depende de ellos (tests).
    explicit SettingsView(const AppContext& ctx, QWidget* parent = nullptr);

signals:
    void toast(const QString& message, const QString& color);

private:
    void refreshGeneral();
    void refreshTracker();
    /// Campos de la configuración del proyecto: el código Jira sólo con Jira; el sistema de GESREQ, si hay catálogo.
    void refreshProject();
    /// Aviso bajo el sistema de GESREQ: qué implica, con qué proyecto choca lo escrito y qué sistemas hay en la bandeja.
    void refreshRequirementSystemNote();
    void commitRequirementSystem();
    void refreshRequirementSource();
    void testRequirementSource();
    /// Elegir el código Jira o el sistema de GESREQ entre los que hay en cada sistema, con búsqueda.
    void pickJiraProject();
    void pickRequirementSystem();
    void refreshCapture();
    void refreshRunShortcuts();
    void refreshCaptureStatus();
    /// Rellena el selector de pantalla con las pantallas conectadas (cambia al conectar un monitor).
    void refreshScreens();
    void testConnection();
    void testZephyr();
    void refreshZephyr();

    SettingsStore& m_settings;
    BugReportService& m_bugs;
    TestPublishService* m_publish;
    RequirementSourceService* m_requirements;
    ProjectStore* m_projects;
    QString m_projectId;
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
    QWidget* m_projectField;
    QWidget* m_projectSection;
    QGridLayout* m_projectCodes;
    QLineEdit* m_jiraProject;
    QWidget* m_jiraProjectField;
    QPushButton* m_jiraProjectPick;
    QLineEdit* m_requirementSystem;
    QWidget* m_requirementSystemField;
    QPushButton* m_requirementSystemPick;
    QLabel* m_requirementSystemNote;
    QCompleter* m_systemCompleter;
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
    QWidget* m_zephyrBlock;
    QCheckBox* m_zephyr;
    QLineEdit* m_zephyrVersion;
    QLineEdit* m_zephyrTestType;
    QPushButton* m_zephyrTest;
    QLabel* m_zephyrNote;
    QPushButton* m_gesreqBadge;
    QLineEdit* m_gesreqUrl;
    QLineEdit* m_gesreqUser;
    QLineEdit* m_gesreqPassword;
    QLabel* m_gesreqSecretNote;
    QLineEdit* m_shortcut;
    QLineEdit* m_recordShortcut;
    QComboBox* m_format;
    QComboBox* m_mode;
    QComboBox* m_screen;
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
    QLineEdit* m_stepNext;
};

} // namespace qaflow
