#pragma once

#include "core/models/Issue.h"

#include <QWidget>
#include <functional>

class QComboBox;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

namespace qaflow {

struct AppContext;
class IssueStore;
class TestCaseStore;
class PlanStore;
class RunHistoryStore;
class IssuePublishService;
class RequirementSourceService;
class QualityRecordService;
class RevisionPublishService;
class BugReportService;
class BugStore;
class ProjectStore;

/// Pantalla "Issues": el punto de entrada para organizar las pruebas de cada requerimiento. A la izquierda,
/// la lista con búsqueda y filtros (estado, prioridad, publicación en Jira) y la consulta de la bandeja de
/// GESREQ; a la derecha, el issue: lo importado del requerimiento (con lo que cambió y si sigue en la
/// bandeja), **la revisión paso a paso**, el plan que lo prueba y los resultados de sus ejecuciones.
///
/// La revisión es el corazón del issue, así que se enseña como lo que es: una serie de pasos —preparar
/// el plan, ejecutarlo, levantar el acta, cerrar la revisión y publicar el resultado—, cada uno con lo
/// que lleva hecho y su acción.
///
/// Lo que se prueba de un requerimiento son **planes**: el issue no agrupa casos sueltos, sus casos son
/// los de sus planes y sus resultados, los de los ciclos de esos planes. Así lo que se ve aquí es sólo
/// del issue, y no cualquier ejecución de un caso que además se use en otro sitio.
class IssuesView : public QWidget {
    Q_OBJECT
public:
    explicit IssuesView(const AppContext& ctx, QWidget* parent = nullptr);
    ~IssuesView() override;

    void focusSearch();
    /// Lee la bandeja de GESREQ y ofrece importar los requerimientos del sistema vinculado al proyecto.
    void consultRequirements();
    /// Abre en este proyecto el issue desde el que se prueba ese requerimiento, creándolo si es la primera
    /// vez (con su issue en el gestor) y reutilizando el que ya hubiera. Lo llama quien coordina el cambio
    /// de proyecto, ya activado.
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
    /// Código de Jira pedido al crear un proyecto: sólo su propia sesión tiene abiertos sus ajustes, así que
    /// lo guarda quien las coordina.
    void projectJiraKeyRequested(const QString& projectId, const QString& jiraProject);

protected:
    void showEvent(QShowEvent* e) override;
    void hideEvent(QHideEvent* e) override;

private:
    void buildListPane(QHBoxLayout* root);
    void buildDetail(QHBoxLayout* root);
    void refreshList();
    void loadDetail();
    void refreshRequirement(const Issue& issue);
    void refreshPlans(const Issue& issue);
    /// Los resultados del issue: los ciclos de sus planes, con lo que salió de cada caso.
    void refreshResults(const Issue& issue);
    /// Los bugs reportados en esas ejecuciones, con su clasificación, su estado y el paso del que salieron.
    void refreshBugs(const Issue& issue);
    /// Aplica un cambio al issue seleccionado sin que el refresco pise lo que se está escribiendo.
    void editSelected(const std::function<void(Issue&)>& mutate);
    void createPlan();
    /// Deja listo el plan con el que se prueba el requerimiento recién importado, si no tiene ninguno.
    QString ensurePlan(const QString& issueId);
    void pickPlan();
    void loadRequirementDetail();
    /// Resuelve en qué proyecto se prueba el requerimiento y, según sea éste u otro, lo abre o lo pide.
    void startTesting(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt);
    /// Ningún proyecto trabaja el sistema del requerimiento: se elige uno existente o se crea, y allí empiezan.
    void askForProject(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt);
    /// Nombre del proyecto que trabaja ese sistema de GESREQ; vacío si ninguno lo tiene vinculado.
    QString projectNameForSystem(const QString& systemCode) const;
    void refreshJira(const Issue& issue);
    /// El issue importado nace ya en el gestor: al traerlo de GESREQ se crea allí su issue, para que los
    /// casos, los bugs y el resultado tengan dónde colgarse desde el principio. Si no se puede (el gestor
    /// sin configurar, sin red), el issue se queda sin publicar y se publica luego desde su tarjeta.
    void publishImported(const QString& issueId);
    /// La tarjeta «Revisión»: los pasos del control de calidad (plan, ejecución, acta, cierre y
    /// publicación) con lo que lleva hecho cada uno, y las revisiones ya cerradas con su acta.
    void refreshRevision(const Issue& issue);
    /// Abre el acta de la revisión, la genera y la guarda donde diga el usuario.
    void generateRecord();
    /// Publica el resultado de la revisión: los planes con sus casos en Zephyr, el resultado y el acta
    /// en el gestor y el registro en GESREQ. Se ofrece cuando la revisión está terminada.
    void publishRevision();
    /// Cierra la revisión en curso con el resultado que se confirme.
    void closeRevision();
    /// Abre la ronda siguiente de pruebas del requerimiento.
    void openRevision();
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
    RevisionPublishService* m_revisionPublish;
    BugReportService* m_bugs;   // sólo para ofrecer el código Jira al crear un proyecto desde la bandeja
    BugStore* m_bugLedger;      // los bugs reportados desde los casos del issue
    QualityRecordService* m_records;
    ProjectStore* m_projects;
    QString m_projectId;
    IssueFilter m_filter;
    bool m_selfEdit = false;
    bool m_loadingDetail = false;
    bool m_consulting = false;
    bool m_readingRequirement = false;
    bool m_publishing = false;

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
    QLabel* m_plansHeader;
    QVBoxLayout* m_plansList;
    QWidget* m_revisionCard;
    QLabel* m_revisionHeader;
    QLabel* m_revisionProgress;
    QVBoxLayout* m_revisionSteps;   // los pasos, que se rehacen en cada refresco
    QVBoxLayout* m_revisionsList;
    QLabel* m_resultsHeader;
    QVBoxLayout* m_resultsList;
    QWidget* m_bugsCard;
    QLabel* m_bugsHeader;
    QVBoxLayout* m_bugsList;
};

} // namespace qaflow
