#pragma once

#include "core/models/Issue.h"
#include "core/models/IssueLink.h"
#include "core/models/IssueProgress.h"
#include "core/models/RunHistory.h"

#include <QPoint>
#include <QWidget>
#include <functional>

class QAction;
class QComboBox;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QSplitter;
class QStackedWidget;
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

/// Pantalla "Issues": el punto de entrada para organizar las pruebas de cada requerimiento. Se abre en el
/// **tablero**: una columna por cómo va el trabajo de QA —pendiente, en preparación, en pruebas, **con
/// casos fallidos o bloqueados** y finalizado— con búsqueda, filtros (prioridad, publicación en Jira) y la
/// consulta de la bandeja de GESREQ, y a la derecha el panel del issue elegido con lo siguiente que toca,
/// los pasos de su revisión y cómo va cada destino. La columna de fallidos no es un estado guardado: sale
/// de los resultados de la revisión en curso, así que un issue sale de ella en cuanto se repite lo roto.
///
/// Al abrir un issue (doble clic, «Abrir el issue», uno nuevo o uno importado) se pasa a su **detalle**,
/// en tres bloques: lo importado del requerimiento (con lo que cambió y si sigue en la bandeja), **la
/// revisión paso a paso** y las rondas ya cerradas.
///
/// La revisión es el corazón del issue, así que se enseña como lo que es: una serie de pasos —preparar
/// el plan, ejecutarlo, revisar los bugs, levantar el acta, cerrar la revisión y publicar el resultado—,
/// cada uno con lo que lleva hecho, su acción y lo que cuelga de él: el plan con sus casos, los ciclos
/// con sus ejecuciones y los bugs. Así el issue no se lee saltando entre tarjetas sueltas.
///
/// La representación en el gestor no es trabajo, es contexto: no tiene tarjeta, es el tag de la cabecera
/// —clave y estado— y de su menú cuelgan publicar, vincular, abrir, consultar el estado y desvincular.
///
/// Lo que se prueba de un requerimiento son **planes**: el issue no agrupa casos sueltos, sus casos son
/// los de sus planes y sus resultados, los de los ciclos de esos planes. Así lo que se ve aquí es sólo
/// del issue, y no cualquier ejecución de un caso que además se use en otro sitio.
class IssuesView : public QWidget {
    Q_OBJECT
public:
    explicit IssuesView(const AppContext& ctx, QWidget* parent = nullptr);
    ~IssuesView() override;

    /// Columnas del tablero, en el orden en que avanza un issue.
    enum class Column { Pending, Preparing, Testing, Broken, Done };
    static constexpr int kColumns = 5;

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
    /// Arrancar un ciclo de ese plan desde el issue: lo hace quien coordina la ejecución, que es quien
    /// sabe si hay algo en curso que lo impida y lleva luego a la pantalla de ejecución.
    void runPlanRequested(const QString& planId);
    /// Continuar ese ciclo con lo que quedó fallado o bloqueado, dentro de la misma revisión.
    void continueCycleRequested(const QString& planRunId);
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
    void resizeEvent(QResizeEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Cómo va la revisión de un issue, lo que cuentan igual la tarjeta del tablero, su panel y los
    /// pasos del detalle: el primero sin hacer (`nextStep`, de 1 a 7) es el que toca.
    struct RevisionSnapshot {
        IssueProgress progress;
        bool hasPlan = false;
        bool executed = false;
        int openBugs = 0;
        bool hasRecord = false;
        bool open = false;       // hay una ronda abierta
        bool closed = false;     // la última ronda está cerrada
        bool published = false;
        int number = 1;          // número de la ronda en curso (o de la última)
        QString continuable;     // ciclo de la ronda que se puede continuar; vacío si ninguno
        /// Qué lleva hecho cada paso, en el orden en que se hacen (plan, ejecución, bugs, acta, cierre
        /// y publicación): es lo que marca el visto y de lo que sale `nextStep`. Un paso posterior puede
        /// estar hecho y uno anterior no —se publica un resultado sin haber levantado el acta—, así que
        /// no basta con comparar con `nextStep`.
        bool done[6] = {};
        int nextStep = 1;
    };
    RevisionSnapshot snapshotOf(const Issue& issue) const;
    Column columnOf(const Issue& issue, const RevisionSnapshot& snapshot) const;

    /// Lo siguiente que toca a un issue, con su acción: lo proponen igual la tarjeta (su botón rápido),
    /// su menú y el panel. La acción elige antes el issue, porque lo que lanza trabaja sobre el elegido.
    struct NextAction {
        QString title;    // «Continuar lo fallado», «Generar el acta (R-213)»…
        QString why;      // por qué toca
        QString hint;     // en pocas palabras, para la tarjeta
        QString button;   // el botón del panel
        QString shortButton;   // el de la tarjeta, que es estrecha
        std::function<void(QWidget*)> run;   // vacía si no hay nada que lanzar desde aquí
        /// Lo otro razonable que se puede hacer ahora, al lado de lo que toca: con la ronda cerrada,
        /// volver a probar **sólo lo que se rompió** en vez de repetir el plan entero. Vacío si no hay.
        QString also;
        QString alsoTip;
        std::function<void(QWidget*)> alsoRun;
    };
    NextAction nextActionOf(const Issue& issue, const RevisionSnapshot& snapshot, Column column);
    /// El menú de una tarjeta (clic derecho o «⋯»): abrir, lo siguiente, ejecutar, continuar, el plan,
    /// el estado de QA, el gestor y GESREQ, y eliminar.
    void showCardMenu(const QString& issueId, const QPoint& globalPos);
    /// Soltar una tarjeta en otra columna: cambia su estado de QA. La de fallidos no admite nada, porque
    /// no es un estado: se sale de ella repitiendo lo roto.
    void moveToColumn(const QString& issueId, Column column);
    void startCardDrag(QPushButton* card);
    /// Resalta la columna sobre la que se arrastra una tarjeta (o la deja como estaba).
    void styleWell(int column, bool hot);

    void buildBoard(QVBoxLayout* root);
    void buildDrawer(QSplitter* root);
    void buildDetail(QVBoxLayout* root);
    /// El tablero: reparte los issues que pasan los filtros por sus columnas.
    void refreshList();
    /// El panel del issue elegido en el tablero.
    void refreshDrawer();
    QWidget* boardCard(const Issue& issue, const RevisionSnapshot& snapshot, Column column);
    /// Pasa del tablero al detalle del issue elegido (o vuelve).
    void showDetail(bool on);
    void loadDetail();
    void refreshRequirement(const Issue& issue);
    /// Los planes del issue con sus casos, dentro del paso que manda prepararlos.
    void fillPlans(const Issue& issue, QVBoxLayout* into);
    /// Los resultados del issue —los ciclos de sus planes, con lo que salió de cada caso—, dentro del
    /// paso que manda ejecutarlo.
    void fillResults(const Issue& issue, const QList<PlanRun>& cycles, QVBoxLayout* into);
    /// Los bugs reportados desde los casos de sus planes, del más reciente al primero.
    QList<IssueLink> bugsOf(const Issue& issue) const;
    /// Esos bugs con su clasificación, su estado y el paso del que salieron, dentro de su paso.
    void fillBugs(const Issue& issue, const QList<IssueLink>& bugs, QVBoxLayout* into);
    /// Aplica un cambio al issue seleccionado sin que el refresco pise lo que se está escribiendo.
    void editSelected(const std::function<void(Issue&)>& mutate);
    void createPlan();
    /// Arranca un ciclo del plan del issue sin salir de esta pantalla; si tiene varios planes que se
    /// puedan ejecutar, pregunta cuál (menú anclado a `anchor`).
    void runPlan(QWidget* anchor);
    /// Planes del issue que se pueden ejecutar ahora: existen, no están archivados y tienen casos.
    QStringList runnablePlans(const Issue& issue) const;
    /// Ciclo de la revisión en curso que se puede continuar (el más reciente que dejó casos rotos);
    /// vacío si no hay ninguno.
    QString continuableCycle(const Issue& issue, int revision) const;
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
    /// El menú del tag del gestor: publicar o vincular mientras no hay issue allí; abrirlo, consultar su
    /// estado o desvincularlo cuando ya lo hay.
    QMenu* buildJiraMenu();
    /// El tag del gestor (clave y estado), lo que tenga pendiente y qué ofrece su menú.
    void refreshJira(const Issue& issue);
    /// El issue importado nace ya en el gestor: al traerlo de GESREQ se crea allí su issue, para que los
    /// casos, los bugs y el resultado tengan dónde colgarse desde el principio. Si no se puede (el gestor
    /// sin configurar, sin red), el issue se queda sin publicar y se publica luego desde su tarjeta.
    void publishImported(const QString& issueId);
    /// La tarjeta «Revisión»: los pasos del control de calidad (plan, ejecución, bugs, acta, cierre y
    /// publicación) con lo que lleva hecho cada uno y lo que cuelga de él, y las rondas ya cerradas.
    void refreshRevision(const Issue& issue);
    /// Abre el acta de la ronda (0 = la que está en curso), la genera y la guarda donde diga el usuario.
    void generateRecord(int revision = 0);
    /// Publica el resultado de una ronda (0 = la que está en curso): los planes con sus casos en Zephyr,
    /// el resultado y el acta en el gestor y el registro en GESREQ. Se ofrece cuando la ronda está
    /// terminada, y desde el historial para acabar de publicar una anterior que se quedó a medias.
    void publishRevision(int revision = 0);
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

    QStackedWidget* m_pages;
    // Tablero
    QPushButton* m_consult;
    QLineEdit* m_search;
    QComboBox* m_priorityFilter;
    QComboBox* m_jiraFilter;
    QLabel* m_listCount;
    QLabel* m_boardEmpty;
    QVBoxLayout* m_columns[kColumns];
    QFrame* m_wells[kColumns];
    QPoint m_dragStart;   // dónde se pulsó la tarjeta que quizá se arrastre
    QLabel* m_columnCounts[kColumns];
    QWidget* m_drawer;
    QSplitter* m_boardSplit;   // tablero | panel del issue: el borde se arrastra
    QVBoxLayout* m_drawerLayout;   // se rehace en cada refresco
    // Detalle
    QWidget* m_empty;
    QWidget* m_detail;
    QLabel* m_idLabel;
    QLabel* m_sourceChip;
    QPushButton* m_jiraChip;   // tag del gestor: clave, estado y, en su menú, lo que se puede hacer
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
    QWidget* m_jiraPending;
    QLabel* m_jiraPendingText;
    QWidget* m_jiraUncertain;
    QLabel* m_jiraUncertainText;
    QPushButton* m_updateJiraButton;
    QAction* m_publishAction;
    QAction* m_linkJiraAction;
    QAction* m_openJiraAction;
    QAction* m_refreshJiraAction;
    QAction* m_unlinkJiraAction;
    QWidget* m_revisionCard;
    QLabel* m_revisionHeader;
    QLabel* m_revisionProgress;
    QVBoxLayout* m_revisionSteps;   // los pasos, que se rehacen en cada refresco
    QWidget* m_historyCard;
    QVBoxLayout* m_revisionsList;
};

} // namespace qaflow
