#pragma once

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

    void navigate(Screen s);
    void setProjectActive(bool active);
    Screen currentScreen() const { return m_current; }
    /// Acción "Finalizar" de la ejecución: sigue con el plan, muestra su informe o vuelve a los casos.
    void finishRun();
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

protected:
    void resizeEvent(QResizeEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    /// Arrastrar ficheros a la ventana los adjunta como evidencia del caso seleccionado.
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    QWidget* buildNavbar();
    void refreshNavbar();
    void runSelectedTarget();
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
    SettingsDialog* m_settings = nullptr;   // se crea al abrirla por primera vez
    Toast* m_toast;
    FlashOverlay* m_flash;
    Screen m_current = Screen::Casos;

    QComboBox* m_projects = nullptr;
    QPushButton* m_projectMenu = nullptr;
    QComboBox* m_runTarget = nullptr;
    QPushButton* m_navRun = nullptr;
    QPushButton* m_navStop = nullptr;
    QPushButton* m_navFinish = nullptr;
    QPushButton* m_navStatus = nullptr;
    QAction* m_actCapture = nullptr;
    QAction* m_actRecord = nullptr;
    QAction* m_actAttach = nullptr;
    QAction* m_trayRecord = nullptr;
    QAction* m_actUndo = nullptr;
    QAction* m_actRun = nullptr;
    QAction* m_actDuplicate = nullptr;
    QAction* m_actDelete = nullptr;
    QAction* m_actReportBug = nullptr;
    QAction* m_actStepPass = nullptr;
    QAction* m_actStepFail = nullptr;
    QAction* m_actStepBack = nullptr;
    QMap<Screen, QAction*> m_screenActions;
    QActionGroup* m_themeGroup = nullptr;
    QActionGroup* m_languageGroup = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QAction* m_trayToggle = nullptr;
    bool m_quitting = false;
    bool m_trayHintShown = false;
};

} // namespace qaflow
