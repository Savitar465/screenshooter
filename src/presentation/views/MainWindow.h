#pragma once

#include "presentation/Screen.h"

#include <QMainWindow>
#include <QMap>

#include <functional>

class QStackedWidget;
class QAction;
class QActionGroup;
class QSystemTrayIcon;
class QMenu;

namespace qaflow {

struct AppContext;
class Sidebar;
class CasesView;
class RunView;
class HistoryView;
class BugView;
class PlanView;
class SettingsView;
class Toast;
class FlashOverlay;

/// Ventana principal: menú, sidebar y pila de pantallas. Coordina la navegación entre vistas,
/// los atajos globales, el icono de la bandeja y los avisos globales (toast, destello de captura,
/// fallos de guardado).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppContext& ctx, QWidget* parent = nullptr);

    void navigate(Screen s);
    Screen currentScreen() const { return m_current; }
    /// Acción "Finalizar" de la ejecución: sigue con el plan, muestra su informe o vuelve a los casos.
    void finishRun();
    void showToast(const QString& message, const QString& color);
    /// Abre el historial en el panel de métricas.
    void showMetrics();
    /// Cierra la aplicación aunque esté configurado "cerrar a la bandeja".
    void quitApplication();

protected:
    void resizeEvent(QResizeEvent* e) override;
    void closeEvent(QCloseEvent* e) override;

private:
    void buildMenus();
    void buildTray();
    void wireSignals();
    void updateCaptureShortcut();
    void updateActions();
    /// Aviso persistente con «Reintentar» cuando un store no pudo escribir en disco.
    void showSaveError(const QString& what, const std::function<bool()>& retry);

    AppContext& m_ctx;
    Sidebar* m_sidebar;
    QStackedWidget* m_stack;
    CasesView* m_cases;
    PlanView* m_plan;
    RunView* m_run;
    HistoryView* m_history;
    BugView* m_bug;
    SettingsView* m_settings;
    Toast* m_toast;
    FlashOverlay* m_flash;
    Screen m_current = Screen::Casos;

    QAction* m_actCapture = nullptr;
    QAction* m_actUndo = nullptr;
    QAction* m_actRun = nullptr;
    QAction* m_actDuplicate = nullptr;
    QAction* m_actDelete = nullptr;
    QAction* m_actReportBug = nullptr;
    QMap<Screen, QAction*> m_screenActions;
    QActionGroup* m_themeGroup = nullptr;
    QActionGroup* m_languageGroup = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QAction* m_trayToggle = nullptr;
    bool m_quitting = false;
    bool m_trayHintShown = false;
};

} // namespace qaflow
