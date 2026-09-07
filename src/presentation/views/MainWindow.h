#pragma once

#include "presentation/Screen.h"

#include <QMainWindow>

class QStackedWidget;
class QShortcut;

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

/// Ventana principal: sidebar + pila de pantallas. Coordina la navegación entre vistas
/// y muestra avisos globales (toast, destello de captura).
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppContext& ctx, QWidget* parent = nullptr);

    void navigate(Screen s);
    /// Acción "Finalizar" de la ejecución: sigue con el plan, muestra su informe o vuelve a los casos.
    void finishRun();
    void showToast(const QString& message, const QString& color);

protected:
    void resizeEvent(QResizeEvent* e) override;

private:
    void wireSignals();
    void updateCaptureShortcut();

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
    QShortcut* m_captureShortcut;
};

} // namespace qaflow
