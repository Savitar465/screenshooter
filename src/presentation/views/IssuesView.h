#pragma once

#include "application/IssueDirectory.h"
#include "core/models/Issue.h"
#include "core/models/IssueLink.h"
#include "core/models/IssueProgress.h"
#include "core/models/PlanReport.h"
#include "core/models/RunHistory.h"

#include <QMessageBox>
#include <QPoint>
#include <QPointer>
#include <QSet>
#include <QWidget>
#include <functional>
#include <optional>

class QAction;
class QBoxLayout;
class QComboBox;
class QGridLayout;
class QFrame;
class QHBoxLayout;
class QLabel;
class QLineEdit;
class QMenu;
class QPushButton;
class QScrollArea;
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
class AttachmentTextService;
class AiService;
class QualityRecordService;
class RevisionPublishService;
class BugReportService;
class BugStore;
class ProjectStore;
class GreqsView;
class IssueListView;

/// Pantalla "Issues": el punto de entrada para organizar las pruebas de cada requerimiento. Se abre en el
/// **tablero**: una columna por cómo va el trabajo de QA —pendiente, en preparación, en pruebas, **con
/// casos fallidos o bloqueados** y finalizado— con búsqueda y filtros (proyecto, prioridad, publicación en
/// Jira), y a la derecha el panel del issue elegido con lo siguiente que toca, los pasos de su revisión y
/// cómo va cada destino.
///
/// El tablero es una **vista general**: enseña también los issues de los demás proyectos, atenuados y con
/// su proyecto, mientras que los del abierto resaltan. De uno ajeno sólo se ve lo que guarda el issue; para
/// seguir con él hay que cambiar a su proyecto, y la pantalla lo pregunta antes de pedirlo.
///
/// Junto al título, la pestaña **GREQS** (`GreqsView`) es la bandeja de GESREQ del usuario: qué
/// requerimientos tienen ya issue y cuáles no, y la búsqueda de uno por su número. La columna de fallidos no es un estado guardado: sale
/// de los resultados de la revisión en curso, así que un issue sale de ella en cuanto se repite lo roto.
///
/// Al abrir un issue (doble clic, «Abrir el issue», uno nuevo o uno importado) se pasa a su **detalle**,
/// en dos columnas: a la izquierda el trabajo —**la revisión**— y a la derecha un panel con el contexto:
/// estado, prioridad, fases, el gestor y el origen, el requerimiento importado (con lo que cambió, si
/// sigue en la bandeja y su ficha plegada) y el historial de rondas cerradas. Cada ronda del historial
/// se abre en esa misma columna del trabajo, para ver lo que salió de ella y terminar lo que le falte.
///
/// La revisión es el corazón del issue, así que se enseña como lo que es: arriba cómo va la ronda (una
/// barra con lo que salió de sus casos y sus cifras) y debajo sus pasos —preparar el plan, ejecutarlo,
/// revisar los bugs, levantar el acta, cerrar la revisión y publicar el resultado— como un stepper. Se
/// ve sólo el paso que toca, con su acción principal destacada y lo que cuelga de él (el plan con sus
/// casos, los ciclos, los destinos); los demás se abren desde su pestaña. Los bugs van siempre a la vista.
///
/// La representación en el gestor no es trabajo, es contexto: no tiene tarjeta, es un tag del panel
/// —clave y estado— y de su menú cuelgan publicar, vincular, abrir, actualizar, consultar el estado y
/// desvincular.
///
/// Lo que se prueba de un requerimiento es **su plan**, uno por issue: el issue no agrupa casos sueltos,
/// sus casos son los de su plan y sus resultados, los de los ciclos de ese plan. Así lo que se ve aquí es
/// sólo del issue, y no cualquier ejecución de un caso que además se use en otro sitio.
class IssuesView : public QWidget {
    Q_OBJECT
public:
    explicit IssuesView(const AppContext& ctx, QWidget* parent = nullptr);
    ~IssuesView() override;

    /// Columnas del tablero, en el orden en que avanza un issue.
    enum class Column { Pending, Preparing, Testing, Broken, Done };
    static constexpr int kColumns = 5;
    /// Días que un issue finalizado sigue en el tablero; pasados, se ve en el historial.
    static constexpr int kDoneDaysOnBoard = 7;

    void focusSearch();
    /// Pasa a la pestaña GREQS (la bandeja de GESREQ del usuario) y la lee si todavía no se leyó.
    void showGreqs();
    /// Cambia entre el tablero y la lista (todos los issues, también los finalizados que ya salieron
    /// del tablero). Se recuerda entre sesiones.
    void setListMode(bool list);
    bool isListMode() const { return m_listMode; }
    /// Abre el detalle de ese issue del proyecto.
    void openIssue(const QString& issueId);
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
    /// Seguir con un issue de otro proyecto, ya confirmado: hay que activarlo (guardando éste) y abrir allí
    /// el issue. La vista no cambia de proyecto por su cuenta.
    void openIssueInProjectRequested(const QString& projectId, const QString& issueId);

protected:
    void showEvent(QShowEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void hideEvent(QHideEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Cómo va la revisión de un issue, lo que cuentan igual la tarjeta del tablero, su panel y los
    /// pasos del detalle: el primero sin hacer (`nextStep`, de 1 a 7) es el que toca. Del mismo modo se
    /// cuenta una ronda anterior abierta desde el historial, que ya está cerrada y no tiene nada que continuar.
    struct RevisionSnapshot {
        IssueProgress progress;
        bool hasPlan = false;
        bool executed = false;
        int openBugs = 0;
        /// Bugs abiertos cuyo paso se volvió a probar y pasó (`retestPassed`): los que se pueden dar por
        /// corregidos y cerrar en el gestor.
        QStringList verifiedBugs;
        bool hasRecord = false;
        bool open = false;       // hay una ronda abierta
        bool closed = false;     // la última ronda está cerrada
        bool published = false;
        int number = 1;          // número de la ronda en curso (o de la última)
        QString phase;           // fase de esa ronda (la de la próxima si todavía no hay ninguna)
        bool finalPhase = true;  // es la última fase: su Conforme cierra el control del requerimiento
        /// La ronda cerrada aprobó una fase que no es la última: no lleva acta ni registro, y lo que
        /// toca es probar en la siguiente (`upcoming`).
        bool phaseApproved = false;
        QString upcoming;        // fase de la próxima ronda
        QString following;       // la fase que va después de `phase`; vacía si ésta es la última
        QString continuable;     // ciclo de la ronda que se puede continuar; vacío si ninguno
        /// Qué lleva hecho cada paso, en el orden en que se hacen (plan, ejecución, bugs, acta, cierre
        /// y publicación): es lo que marca el visto y de lo que sale `nextStep`. Un paso posterior puede
        /// estar hecho y uno anterior no —se publica un resultado sin haber levantado el acta—, así que
        /// no basta con comparar con `nextStep`.
        bool done[6] = {};
        int nextStep = 1;
    };
    /// `revision` 0 es la ronda en curso (la abierta o, si ninguna lo está, la última); otro número, esa
    /// ronda anterior.
    RevisionSnapshot snapshotOf(const Issue& issue, int revision = 0) const;
    /// El resultado de una ronda tal y como se lee según su fase: «Aprobada en QA», «Conforme», «Observado».
    QString outcomeText(const Issue& issue, QaOutcome outcome, const QString& phase) const;
    /// Las fases del proyecto con cómo va el issue en cada una: «QA ✓ → PRE ●».
    QString phaseTrack(const Issue& issue, const RevisionSnapshot& snapshot) const;
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

    // ---- Vista general: los issues de los demás proyectos ------------------------------------------
    /// La columna de un issue de otro proyecto. De él sólo se tiene lo guardado (no sus planes ni sus
    /// ciclos), así que va por su estado de QA y por cómo se cerró su última revisión.
    static Column foreignColumnOf(const Issue& issue);
    QWidget* foreignCard(const IssueDirectory::Entry& entry, Column column);
    /// El panel de un issue de otro proyecto: lo que guarda y cómo seguir con él.
    void refreshForeignDrawer(const IssueDirectory::Entry& entry);
    void showForeignMenu(const QString& projectId, const QString& issueId, const QPoint& globalPos);
    /// Elige en el tablero un issue de otro proyecto (vacío: ninguno).
    void selectForeign(const QString& projectId, const QString& issueId);
    std::optional<IssueDirectory::Entry> selectedForeign() const;
    /// Seguir con un issue de otro proyecto: pregunta antes, porque cambia de proyecto.
    void openElsewhere(const QString& projectId, const QString& issueId);
    QString projectName(const QString& projectId) const;

    /// Páginas de la pantalla, en el orden de `m_pages`.
    enum class Page { Board = 0, Detail = 1, Greqs = 2 };
    /// Las pestañas de la pantalla (Issues | GREQS) para la cabecera de la página `page`. Cada
    /// página tiene las suyas, así que se marcan todas a la vez según la página que se ve (`syncTabs`).
    QWidget* screenTabs(Page page);
    void syncTabs();
    /// ¿Sigue en el tablero? Un finalizado hace más de `kDoneDaysOnBoard` días sólo está en la lista.
    static bool onBoard(const Issue& issue);
    /// Pone los filtros al lado de las pestañas si caben, o en una segunda fila si la ventana es estrecha.
    void placeHeader();
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
    /// Pone el panel lateral del detalle a la derecha o, si la ventana es estrecha, debajo.
    void placeDetailColumns();
    /// Enseña en el detalle el paso `index` de la revisión (los demás quedan en su pestaña del stepper).
    void showStep(int index);
    void loadDetail();
    void refreshRequirement(const Issue& issue);
    /// Una fila plegable de las listas del detalle (un plan con sus casos, un ciclo con sus ejecuciones):
    /// la cabecera, con su chevron, y debajo el cuerpo. Se abre y se cierra al pulsar la cabecera, y
    /// queda como se dejó entre refrescos (`m_expanded`, por `key`).
    struct Collapsible {
        QFrame* header = nullptr;
        QHBoxLayout* head = nullptr;
        QVBoxLayout* body = nullptr;
    };
    Collapsible collapsible(const QString& key, QVBoxLayout* into);
    /// Abre o cierra la fila plegable de esa cabecera.
    void toggleSection(QObject* header);
    /// El plan del issue con sus casos, dentro del paso que manda prepararlo.
    void fillPlan(const Issue& issue, QVBoxLayout* into);
    /// Los resultados del issue —los ciclos de su plan, con lo que salió de cada caso—, dentro del
    /// paso que manda ejecutarlo. Los de una ronda anterior (`past`) se enseñan sin ofrecer continuarlos:
    /// lo que quedó roto se continúa desde la ronda en curso.
    void fillResults(const Issue& issue, const QList<PlanRun>& cycles, QVBoxLayout* into, bool past = false);
    /// Los bugs reportados desde los casos de su plan, del más reciente al primero.
    QList<IssueLink> bugsOf(const Issue& issue) const;
    /// Esos bugs con su clasificación, su estado y el paso del que salieron, dentro de su paso.
    void fillBugs(const Issue& issue, const QList<IssueLink>& bugs, const QStringList& verified, QVBoxLayout* into);
    /// Pregunta y cierra en el gestor esos bugs (los verificados del issue elegido).
    void closeVerifiedBugs(const QStringList& keys);
    /// Menú para elegir en qué fases se prueba el issue elegido (sólo QA, sólo PRE…).
    void choosePhases(QWidget* anchor);
    /// Pregunta y sube a Zephyr el plan del issue entero (`RevisionPublishService::uploadPlan`); con
    /// `sync`, reescribiendo antes sus Tests.
    void uploadPlan(bool sync);
    /// Aplica un cambio al issue seleccionado sin que el refresco pise lo que se está escribiendo.
    void editSelected(const std::function<void(Issue&)>& mutate);
    void createPlan();
    /// Arranca un ciclo del plan del issue sin salir de esta pantalla (lo coordina quien ejecuta).
    void runPlan();
    /// El plan del issue si se puede ejecutar ahora (existe, no está archivado y tiene casos); vacío si no.
    QString runnablePlan(const Issue& issue) const;
    /// Ciclo de la revisión en curso que se puede continuar (el más reciente que dejó casos rotos);
    /// vacío si no hay ninguno.
    QString continuableCycle(const Issue& issue, int revision) const;
    /// Lo que repetiría continuar ese ciclo, con lo que tiene hoy su plan (`PlanReport::toContinue`);
    /// vacío si el ciclo no terminó o no le queda nada.
    QStringList continuationOf(const PlanReport& report) const;
    /// Deja listo el plan con el que se prueba el requerimiento recién importado, si no tiene ninguno.
    QString ensurePlan(const QString& issueId);
    void pickPlan();
    /// Genera casos para el plan del issue con una IA externa (prompt copiado, respuesta pegada), revisados
    /// antes de añadirlos. Sin plan, se le crea uno al aceptar.
    void generateCases();
    void loadRequirementDetail();
    /// Resuelve en qué proyecto se prueba el requerimiento y, según sea éste u otro, lo abre o lo pide.
    void startTesting(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt);
    /// Ningún proyecto trabaja el sistema del requerimiento: se elige uno existente o se crea, y allí empiezan.
    void askForProject(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt);
    /// Nombre del proyecto que trabaja ese sistema de GESREQ; vacío si ninguno lo tiene vinculado.
    QString projectNameForSystem(const QString& systemCode) const;
    /// El menú del tag del gestor: publicar o vincular mientras no hay issue allí; abrirlo, reescribirlo,
    /// consultar su estado o desvincularlo cuando ya lo hay.
    QMenu* buildJiraMenu();
    /// El tag del gestor (clave y estado), si quedó sin confirmar un envío y qué ofrece su menú.
    void refreshJira(const Issue& issue);
    /// El issue importado nace ya en el gestor: al traerlo de GESREQ se crea allí su issue, para que los
    /// casos, los bugs y el resultado tengan dónde colgarse desde el principio. Si no se puede (el gestor
    /// sin configurar, sin red), el issue se queda sin publicar y se publica luego desde su tarjeta.
    void publishImported(const QString& issueId);
    /// La tarjeta «Revisión»: los pasos del control de calidad (plan, ejecución, bugs, acta, cierre y
    /// publicación) con lo que lleva hecho cada uno y lo que cuelga de él, y las rondas ya cerradas.
    ///
    /// Enseña la ronda en curso o, si se eligió en el historial, una anterior (`m_viewedRevision`). De
    /// ésa se ve lo que salió —sus ciclos, sus bugs, su acta y dónde llegó su resultado— y sólo se
    /// ofrece lo que sigue siendo suyo: levantar o regenerar su acta y completar su publicación. Lo
    /// demás (el plan, Zephyr, ejecutar, continuar, cerrar, abrir otra ronda) trabaja sobre la ronda en
    /// curso, así que se hace desde ella.
    void refreshRevision(const Issue& issue);
    /// Enseña en el detalle esa ronda del issue elegido (0 o la última: la ronda en curso).
    void viewRevision(int revision);
    /// Abre el acta de la ronda (0 = la que está en curso), la genera y la guarda donde diga el usuario.
    void generateRecord(int revision = 0);
    /// Publica el resultado de una ronda (0 = la que está en curso): los planes con sus casos en Zephyr,
    /// el resultado y el acta en el gestor y el registro en GESREQ. Se ofrece cuando la ronda está
    /// terminada, y desde una ronda anterior abierta del historial para acabar de publicarla si se quedó a
    /// medias.
    void publishRevision(int revision = 0);
    /// Cierra la revisión en curso con el resultado que se confirme y sube sus resultados a Zephyr y al
    /// gestor (`RevisionPublishService::closeRevision`).
    void closeRevision();
    /// Lo que le falta por publicar a esa ronda, para decirlo: «GESREQ y el gestor».
    QString pendingText(const Issue& issue, int revision) const;
    /// Abre la ronda siguiente de pruebas del requerimiento.
    void openRevision();
    /// Crear la representación en el gestor, revisándola antes; con `update`, reescribir la ya publicada.
    void openPublishDialog(bool update);
    void publishToJira();
    void linkJira();
    void unlinkJira();
    void refreshJiraStatus();
    void removeSelected();
    const Issue* selected() const;

    IssueStore& m_issues;
    TestCaseStore& m_cases;
    PlanStore& m_plans;
    RunHistoryStore& m_history;
    RequirementSourceService* m_requirements;
    AttachmentTextService* m_attachmentText = nullptr;
    AiService* m_ai = nullptr;
    IssuePublishService* m_publish;
    RevisionPublishService* m_revisionPublish;
    BugReportService* m_bugs;   // sólo para ofrecer el código Jira al crear un proyecto desde la bandeja
    BugStore* m_bugLedger;      // los bugs reportados desde los casos del issue
    QualityRecordService* m_records;
    ProjectStore* m_projects;
    IssueDirectory* m_directory;   // los issues de todos los proyectos; nullptr = sólo los de éste
    QString m_projectId;
    bool m_allProjects = true;     // el tablero enseña también los issues de los demás proyectos
    QString m_foreignProject;      // issue de otro proyecto elegido en el tablero
    QString m_foreignIssue;
    /// La confirmación de cambio de proyecto abierta, si la hay: nunca dos a la vez. Dos ventanas modales
    /// de la misma ventana se bloquean entre sí y ninguna deja pulsar sus botones.
    QPointer<QMessageBox> m_switchConfirm;
    IssueFilter m_filter;
    bool m_selfEdit = false;
    bool m_loadingDetail = false;
    bool m_readingRequirement = false;
    bool m_publishing = false;

    // La cabecera del tablero: lo que se ve (izquierda) y la búsqueda con los filtros (derecha), en una fila
    // o, si la ventana no da para tanto, en dos.
    QGridLayout* m_headGrid;
    QWidget* m_headLeft;
    QWidget* m_headFilters;
    bool m_headTwoRows = false;
    QStackedWidget* m_pages;   // tablero, detalle y GREQS
    GreqsView* m_greqs;
    // Tablero o lista: las dos vistas del trabajo, con los mismos filtros y el mismo panel del issue.
    bool m_listMode = false;
    QStackedWidget* m_boardViews;
    IssueListView* m_list;
    QPushButton* m_boardToggle;
    QPushButton* m_listToggle;
    QList<QPushButton*> m_tabs;     // las pestañas de todas las páginas, con su página en `tabPage`
    // Tablero
    QLineEdit* m_search;
    QComboBox* m_scopeFilter;
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
    // Detalle: la columna del trabajo (la revisión) y, al lado, el panel con los datos del issue.
    QWidget* m_empty;
    QWidget* m_detail;
    QBoxLayout* m_detailColumns;
    QWidget* m_sidebar;
    QLabel* m_idLabel;
    QLabel* m_sourceChip;
    QPushButton* m_jiraChip;   // tag del gestor: clave, estado y, en su menú, lo que se puede hacer
    QLineEdit* m_title;
    QComboBox* m_state;
    QComboBox* m_priority;
    QLabel* m_phaseTrack;
    QPushButton* m_phasesButton;
    QWidget* m_requirementCard;
    QWidget* m_changes;
    QLabel* m_changesText;
    QWidget* m_missing;
    QLabel* m_missingText;
    QLabel* m_requirementSummary;
    QPushButton* m_toggleRequirement;
    QWidget* m_requirementMore;   // la ficha entera, plegada: se abre con `m_toggleRequirement`
    QLabel* m_requirementInfo;
    QPushButton* m_loadDetail;
    QPushButton* m_openRequirement;
    QLabel* m_detailInfo;
    QVBoxLayout* m_attachments;
    QWidget* m_jiraUncertain;
    QLabel* m_jiraUncertainText;
    QAction* m_publishAction;
    QAction* m_linkJiraAction;
    QAction* m_openJiraAction;
    QAction* m_updateJiraAction;
    QAction* m_refreshJiraAction;
    QAction* m_unlinkJiraAction;
    QWidget* m_revisionCard;
    QScrollArea* m_detailScroll;
    QWidget* m_pastBanner;      // «Estás viendo la revisión N»: con la vuelta a la ronda en curso
    QLabel* m_pastBannerText;
    int m_viewedRevision = 0;   // ronda anterior que se ve en el detalle; 0 = la ronda en curso
    QLabel* m_revisionHeader;
    QLabel* m_revisionProgress;
    QVBoxLayout* m_progressBar;   // la barra de resultados de la ronda, que se rehace en cada refresco
    QLabel* m_tileExecuted;
    QLabel* m_tilePassed;
    QLabel* m_tileFailed;
    QLabel* m_tileBlocked;
    QHBoxLayout* m_stepper;         // una pestaña por paso, que se rehacen en cada refresco
    QVBoxLayout* m_revisionSteps;   // la tarjeta de cada paso; sólo se ve la del elegido
    QList<QPushButton*> m_stepTabs;
    QList<QWidget*> m_stepCards;
    QList<QString> m_stepColors;    // color de la barra de cada pestaña: hecho, el que toca o pendiente
    int m_stepChoice = -1;          // paso elegido a mano; -1 = el que toca
    int m_stepCurrent = -1;         // el que tocaba en el último refresco: si cambia, se vuelve a él
    QString m_stepIssue;            // issue del último refresco: otro issue empieza por el paso que toca
    int m_stepRevision = 0;         // ronda vista en el último refresco: otra ronda también
    QSet<QString> m_expanded;       // filas plegables abiertas: «plan:PL-0001», «cycle:PR-0003»
    QWidget* m_bugsSection;
    QLabel* m_bugsHeader;
    QVBoxLayout* m_bugsList;
    QWidget* m_historyCard;
    QVBoxLayout* m_revisionsList;
};

} // namespace qaflow
