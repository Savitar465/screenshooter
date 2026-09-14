#pragma once

#include <QDialog>
#include <QString>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QWidget;

namespace qaflow {

class ProjectStore;
class BugReportService;
class RequirementSourceService;

/// Alta de proyecto con sus dos códigos opcionales: el proyecto de Jira donde se publican sus issues y el
/// sistema de GESREQ cuyos requerimientos se prueban en él. Ambos se pueden dejar vacíos y configurar luego
/// en «Configuración del proyecto»; los dos se eligen con «Buscar…» de lo que hay en cada sistema.
///
/// Se abre también desde la bandeja de GESREQ, cuando se quieren empezar las pruebas de un requerimiento
/// cuyo sistema no trabaja ningún proyecto: entonces llega ese sistema ya escrito y, además de crear uno
/// nuevo, se puede vincular a un proyecto que ya existe (lo habitual cuando está creado pero sin vincular).
///
/// Al aceptar crea el proyecto y guarda el sistema: `projectId()` es el proyecto resultante. El código Jira
/// no se guarda aquí —vive en los ajustes de cada proyecto, que sólo tiene abiertos su sesión—, se devuelve
/// en `jiraProject()` para que lo aplique quien coordina las sesiones.
class ProjectSetupDialog : public QDialog {
    Q_OBJECT
public:
    /// `system`: sistema de GESREQ que se quiere trabajar, ya escrito (vacío en el alta normal).
    /// `allowExisting`: si además de crear uno nuevo se puede elegir un proyecto que ya existe.
    /// `bugs` y `requirements` pueden ser nullptr: sin ellos no se ofrece «Buscar…» (tests).
    ProjectSetupDialog(ProjectStore& projects, const QString& system, bool allowExisting, BugReportService* bugs,
                       RequirementSourceService* requirements, QWidget* parent = nullptr);

    /// Proyecto creado o elegido; sólo tiene valor tras aceptar.
    QString projectId() const { return m_projectId; }
    /// Código del proyecto de Jira que se pidió, para aplicarlo a los ajustes de ese proyecto; puede ser vacío.
    QString jiraProject() const { return m_jiraProject; }

    void accept() override;

private:
    /// Enseña u oculta el nombre y el código Jira según se cree un proyecto o se elija uno existente.
    void refresh();
    /// Aviso de debajo: con qué proyecto choca el sistema escrito, o qué deja de trabajar el elegido.
    void refreshNote();
    void setNote(const QString& text, bool warning);
    void pickJiraProject();
    void pickRequirementSystem();
    /// Proyecto elegido en el selector; vacío si se está creando uno nuevo.
    QString target() const;

    ProjectStore& m_projects;
    BugReportService* m_bugs;
    RequirementSourceService* m_requirements;
    QString m_projectId;
    QString m_jiraProject;
    /// Proyecto ya creado en un intento anterior que no llegó a terminar (un fallo al guardar el sistema):
    /// al reintentar se reutiliza en vez de crear otro.
    QString m_created;

    QComboBox* m_target = nullptr;   // nullptr cuando sólo se puede crear
    QWidget* m_nameField;
    QLineEdit* m_name;
    QWidget* m_jiraField;
    QLineEdit* m_jira;
    QPushButton* m_jiraPick;
    QLineEdit* m_system;
    QPushButton* m_systemPick;
    QLabel* m_note;
};

} // namespace qaflow
