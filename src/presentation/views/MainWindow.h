#pragma once

#include "core/models/Requirement.h"
#include "presentation/Screen.h"

#include <QMainWindow>
#include <QList>
#include <QMap>
#include <QUrl>

#include <functional>

class QStackedWidget;
class QAction;
class QActionGroup;
class QSystemTrayIcon;
class QMenu;
class QComboBox;
class QPushButton;
class QLabel;

namespace qaflow {

struct AppContext;
class Sidebar;
class StatusStrip;
class CasesView;
class RunView;
class HistoryView;
class BugView;
class IssuesView;
class PlanView;
class SettingsDialog;
class Toast;
class FlashOverlay;

/// Ventana principal: menú, rail de navegación, pila de pantallas y barra de estado. Coordina la navegación entre vistas,
/// los atajos globales, el icono de la bandeja y los avisos globales (toast, destello de captura,
/// fallos de guardado).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppContext& ctx, QWidget* parent = nullptr);

    /// Va a la raíz de esa sección: es lo que hacen el rail, el menú y los atajos, así que vacía el
    /// camino de vuelta (el botón «atrás» desaparece).
    void navigate(Screen s);
    /// Entra en profundidad, al estilo de iOS: guarda la pantalla actual para que el botón «atrás»
    /// vuelva a ella con su contexto (el plan desde el que se creó el caso, el issue desde el que se
    /// abrió el plan). Es lo que hacen las acciones de las tarjetas, no la navegación del rail.
    void navigateInto(Screen s);
    /// Vuelve a la pantalla anterior del camino. No hace nada si no hay ninguna.
    void goBack();
    void setProjectActive(bool active);
    Screen currentScreen() const { return m_current; }
    /// Acción "Finalizar" de la ejecución: sigue con el plan, muestra su informe o vuelve a los casos.
    void finishRun();
    /// Abre en este proyecto, ya activo, el issue desde el que se prueba ese requerimiento de GESREQ,
    /// creándolo si es la primera vez. Lo llama quien coordina el cambio de proyecto.
    void startTesting(const ExternalRequirement& requirement, const QString& connection, const QDateTime& fetchedAt);
    void showToast(const QString& message, const QString& color);
    /// Abre el historial en el panel de métricas.
    void showMetrics();
    /// Dice en qué paso ha quedado la ejecución tras usar uno de sus atajos globales. Con la ventana
    /// en segundo plano (lo normal mientras se prueba otra aplicación) el aviso va a la bandeja.
    void announceRunStep();
    /// Cierra la aplicación aunque esté configurado "cerrar a la bandeja".
    void quitApplication();
    /// Abre la ventana de ajustes («Archivo → Ajustes») o la trae al frente si ya estaba abierta.
    void openSettings();
    /// Ventana de ajustes mientras esté abierta; nullptr si no lo está. La usan la reconstrucción
    /// de la ventana al cambiar de idioma o tema y las capturas de la documentación.
    QWidget* settingsWindow() const;
    /// Adjunta ficheros locales (rutas o URLs file://) al caso seleccionado; lo usan el arrastre a
    /// la ventana y el menú.
    void attachFiles(const QList<QUrl>& urls);

signals:
    void projectSwitchRequested(const QString& id);
    /// Las pruebas de ese requerimiento se hacen en otro proyecto: hay que guardar éste, activar aquél y
    /// abrir allí su issue. La ventana no cambia de proyecto por su cuenta.
    void startTestingRequested(const QString& projectId, const ExternalRequirement& requirement,
                               const QString& connection, const QDateTime& fetchedAt);
    /// Código de Jira elegido al crear un proyecto: los ajustes son de cada proyecto y sólo los tiene
    /// abiertos su sesión, así que lo guarda quien las coordina.
    void projectJiraKeyRequested(const QString& projectId, const QString& jiraProject);

protected:
    void resizeEvent(QResizeEvent* e) override;
    /// El botón «atrás» del ratón vuelve por el camino, como en el navegador.
    void mousePressEvent(QMouseEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    /// Arrastrar ficheros a la ventana los adjunta como evidencia del caso seleccionado.
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QWidget* buildNavbar();
    void refreshNavbar();
    /// Cambia de pantalla sin tocar el camino de vuelta; `navigate` y `navigateInto` lo usan.
    void showScreen(Screen s);
    /// Pone al día el botón «atrás» y su acción del menú con lo último del camino.
    void refreshBackButton();
    /// Nombre con el que el camino de vuelta anuncia una pantalla, con su contexto cuando lo tiene
    /// («Suite de regresión», «Issue ISS-0003»).
    QString screenLabel(Screen s) const;
    /// Issue que se prueba con ese ciclo de plan; vacío si el plan no prueba ningún requerimiento.
    QString issueOfPlanRun(const QString& planRunId) const;
    /// A qué requerimiento y a qué ronda va a pertenecer un ciclo de ese plan («GREQ 2026997 · revisión 2»);
    /// vacío si el plan no prueba ningún issue.
    QString cycleContext(const QString& planId) const;
    /// Pregunta en qué ambiente se va a probar y, si se acepta, arranca el ciclo (`beginPlanRun`).
    void askCycleEnvironment(const QString& planId, const QString& planName);
    /// Arranca el ciclo del plan en ese ambiente y lleva a la ejecución.
    void beginPlanRun(const QString& planId, const QString& environment);
    /// Continúa un ciclo terminado con sus casos fallados y bloqueados: comprueba que se puede,
    /// pregunta el ambiente y arranca la continuación.
    void continueCycleRun(const QString& planRunId);
    void beginContinuation(const QString& planRunId, const QString& environment);
    void runSelectedTarget();
    /// Arranca un ciclo de ese plan y lleva a la ejecución; avisa si hay algo en curso que lo impida.
    void startPlanRun(const QString& planId);
    void selectContextTarget();
    void buildMenus();
    void buildTray();
    void wireSignals();
    void updateShortcuts();
    void updateActions();
    /// Aviso persistente con «Reintentar» cuando un store no pudo escribir en disco.
    void showSaveError(const QString& what, const std::function<bool()>& retry);

    AppContext& m_ctx;
    Sidebar* m_sidebar;
    StatusStrip* m_status;
    QStackedWidget* m_stack;
    CasesView* m_cases;
    PlanView* m_plan;
    RunView* m_run;
    HistoryView* m_history;
    BugView* m_bug;
    IssuesView* m_issuesView;
    SettingsDialog* m_settings = nullptr;   // se crea al abrirla por primera vez
    Toast* m_toast;
    FlashOverlay* m_flash;
    Screen m_current = Screen::Casos;
    /// Camino de vuelta: cada paso guarda de qué pantalla se vino y con qué nombre anunciarla, tomado
    /// en el momento de salir para que diga el plan o el issue que se estaba mirando y no el de ahora.
    struct BackStep {
        Screen screen = Screen::Casos;
        QString label;
    };
    QList<BackStep> m_back;

    QComboBox* m_projects = nullptr;
    QPushButton* m_projectMenu = nullptr;
    QPushButton* m_navBack = nullptr;
    QComboBox* m_runTarget = nullptr;
    QPushButton* m_navRun = nullptr;
    QPushButton* m_navStop = nullptr;
    QPushButton* m_navFinish = nullptr;
    QPushButton* m_navStatus = nullptr;
    QAction* m_actCapture = nullptr;
    QAction* m_actRecord = nullptr;
    QAction* m_actAttach = nullptr;
    QAction* m_trayRecord = nullptr;
    QAction* m_actBack = nullptr;
    QAction* m_actUndo = nullptr;
    QAction* m_actRun = nullptr;
    QAction* m_actDuplicate = nullptr;
    QAction* m_actDelete = nullptr;
    QAction* m_actReportBug = nullptr;
    QAction* m_actStepPass = nullptr;
    QAction* m_actStepFail = nullptr;
    QAction* m_actStepBack = nullptr;
    QAction* m_actStepNext = nullptr;
    QMap<Screen, QAction*> m_screenActions;
    QActionGroup* m_themeGroup = nullptr;
    QActionGroup* m_languageGroup = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QAction* m_trayToggle = nullptr;
    bool m_quitting = false;
    bool m_trayHintShown = false;
};

} // namespace qaflow
