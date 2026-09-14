#pragma once

#include "core/models/Issue.h"

#include <QWidget>
#include <functional>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace qaflow {

struct AppContext;
class IssueStore;
class TestCaseStore;
class PlanStore;
class RunHistoryStore;
class IssuePublishService;
class RequirementSourceService;
class ProjectStore;
class TextArea;

/// Pantalla "Issues": el punto de entrada para organizar las pruebas de cada requerimiento. A la izquierda,
/// la lista con búsqueda y filtros (estado, prioridad, publicación en Jira) y la consulta de la bandeja de
/// GESREQ; a la derecha, el issue: lo importado del requerimiento (con lo que cambió y si sigue en la
/// bandeja), su estado y notas de QA, sus casos y planes y los resultados de sus pruebas.
class IssuesView : public QWidget {
    Q_OBJECT
public:
    explicit IssuesView(const AppContext& ctx, QWidget* parent = nullptr);
    ~IssuesView() override;

    void focusSearch();
    /// Lee la bandeja de GESREQ y ofrece importar los requerimientos del sistema vinculado al proyecto.
    void consultRequirements();
    /// Abre en este proyecto el issue desde el que se prueba ese requerimiento, creándolo si es la primera
    /// vez y reutilizando el que ya hubiera. Lo llama quien coordina el cambio de proyecto, ya activado.
    void openRequirement(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt);

signals:
    void toast(const QString& message, const QString& color);
    void openCaseRequested(const QString& caseId);
    void openPlanRequested(const QString& planId);
    void openRunRequested(const QString& runId);
    void openUrlRequested(const QString& url);
    /// Falta configurar algo (el sistema de GESREQ del proyecto): hay que abrir los ajustes.
    void settingsRequested();
    /// Las pruebas de ese requerimiento son de otro proyecto: hay que activarlo (guardando este) y abrir
    /// allí su issue. La vista no cambia de proyecto por su cuenta.
    void startTestingRequested(const QString& projectId, const ExternalRequirement& requirement,
                               const QString& connection, const QDateTime& fetchedAt);

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    void buildListPane(QHBoxLayout* root);
    void buildDetail(QHBoxLayout* root);
    void refreshList();
    void loadDetail();
    void refreshRequirement(const Issue& issue);
    void refreshCases(const Issue& issue);
    void refreshPlans(const Issue& issue);
    void refreshResults(const Issue& issue);
    /// Aplica un cambio al issue seleccionado sin que el refresco pise lo que se está escribiendo.
    void editSelected(const std::function<void(Issue&)>& mutate);
    /// Guarda las notas que se estaban escribiendo (se guardan con un pequeño retraso, no a cada tecla).
    void commitNotes();
    void createCase();
    void pickCase();
    void createPlan();
    void pickPlan();
    void loadRequirementDetail();
    /// Resuelve en qué proyecto se prueba el requerimiento y, según sea éste u otro, lo abre o lo pide.
    void startTesting(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt);
    /// Nombre del proyecto que trabaja ese sistema de GESREQ; vacío si ninguno lo tiene vinculado.
    QString projectNameForSystem(const QString& systemCode) const;
    void refreshJira(const Issue& issue);
    /// Crear la representación en el gestor, revisándola antes; con `update`, reescribir la ya publicada.
    void openPublishDialog(bool update);
    void publishToJira();
    void linkJira();
    void unlinkJira();
    void refreshJiraStatus();
    void removeSelected();
    /// Sistema de GESREQ vinculado al proyecto abierto; vacío si ninguno (o sin catálogo de proyectos).
    QString linkedSystem() const;
    const Issue* selected() const;

    IssueStore& m_issues;
    TestCaseStore& m_cases;
    PlanStore& m_plans;
    RunHistoryStore& m_history;
    RequirementSourceService* m_requirements;
    IssuePublishService* m_publish;
    ProjectStore* m_projects;
    QString m_projectId;
    IssueFilter m_filter;
    bool m_selfEdit = false;
    bool m_loadingDetail = false;
    bool m_consulting = false;
    bool m_readingRequirement = false;
    bool m_publishing = false;
    QString m_notesIssueId;   // issue al que pertenecen las notas pendientes de guardar

    // Lista
    QPushButton* m_consult;
    QLineEdit* m_search;
    QComboBox* m_stateFilter;
    QComboBox* m_priorityFilter;
    QComboBox* m_jiraFilter;
    QLabel* m_listCount;
    QVBoxLayout* m_listLayout;
    // Detalle
    QWidget* m_empty;
    QWidget* m_detail;
    QLabel* m_idLabel;
    QLabel* m_sourceChip;
    QLabel* m_jiraChip;
    QLineEdit* m_title;
    QComboBox* m_state;
    QComboBox* m_priority;
    QWidget* m_requirementCard;
    QWidget* m_changes;
    QLabel* m_changesText;
    QWidget* m_missing;
    QLabel* m_missingText;
    QLabel* m_requirementInfo;
    QPushButton* m_loadDetail;
    QPushButton* m_openRequirement;
    QLabel* m_detailInfo;
    QVBoxLayout* m_attachments;
    QWidget* m_jiraCard;
    QLabel* m_jiraInfo;
    QWidget* m_jiraPending;
    QLabel* m_jiraPendingText;
    QWidget* m_jiraUncertain;
    QLabel* m_jiraUncertainText;
    QPushButton* m_publishButton;
    QPushButton* m_linkJiraButton;
    QPushButton* m_openJiraButton;
    QPushButton* m_refreshJiraButton;
    QPushButton* m_updateJiraButton;
    QPushButton* m_unlinkJiraButton;
    TextArea* m_notes;
    QTimer* m_notesTimer;
    QLabel* m_casesHeader;
    QVBoxLayout* m_casesList;
    QLabel* m_plansHeader;
    QVBoxLayout* m_plansList;
    QLabel* m_resultsHeader;
    QVBoxLayout* m_resultsList;
};

} // namespace qaflow
