#include "MainWindow.h"

#include "application/AppContext.h"
#include "presentation/theme/Theme.h"
#include "presentation/views/BugView.h"
#include "presentation/views/CasesView.h"
#include "presentation/views/HistoryView.h"
#include "presentation/views/PlanView.h"
#include "presentation/views/RunView.h"
#include "presentation/views/SettingsView.h"
#include "presentation/views/Sidebar.h"
#include "presentation/widgets/FlashOverlay.h"
#include "presentation/widgets/Toast.h"
#include "presentation/widgets/Ui.h"

#include <QShortcut>
#include <QStackedWidget>

namespace qaflow {

MainWindow::MainWindow(AppContext& ctx, QWidget* parent) : QMainWindow(parent), m_ctx(ctx) {
    setWindowTitle(QStringLiteral("QAflow"));
    setMinimumSize(1100, 720);
    resize(1360, 860);

    auto* central = new QWidget;
    central->setObjectName(QStringLiteral("central"));
    auto* h = ui::hbox(central, 0, 0);

    m_sidebar = new Sidebar(*ctx.cases, *ctx.plan, *ctx.run, *ctx.history);
    h->addWidget(m_sidebar);

    m_stack = new QStackedWidget;
    m_cases = new CasesView(*ctx.cases, *ctx.run, *ctx.history);
    m_plan = new PlanView(*ctx.cases, *ctx.plan);
    m_run = new RunView(*ctx.cases, *ctx.run, *ctx.settings);
    m_history = new HistoryView(*ctx.cases, *ctx.history);
    m_bug = new BugView(*ctx.cases, *ctx.settings, *ctx.bugs);
    m_settings = new SettingsView(*ctx.settings, *ctx.bugs);
    m_stack->insertWidget(static_cast<int>(Screen::Casos), m_cases);
    m_stack->insertWidget(static_cast<int>(Screen::Plan), m_plan);
    m_stack->insertWidget(static_cast<int>(Screen::Run), m_run);
    m_stack->insertWidget(static_cast<int>(Screen::Historial), m_history);
    m_stack->insertWidget(static_cast<int>(Screen::Bug), m_bug);
    m_stack->insertWidget(static_cast<int>(Screen::Ajustes), m_settings);
    h->addWidget(m_stack, 1);
    setCentralWidget(central);

    m_toast = new Toast(central);
    m_flash = new FlashOverlay(central);
    m_captureShortcut = new QShortcut(this);
    m_captureShortcut->setContext(Qt::ApplicationShortcut);

    wireSignals();
    updateCaptureShortcut();
    navigate(Screen::Casos);
}

void MainWindow::wireSignals() {
    connect(m_sidebar, &Sidebar::navigate, this, &MainWindow::navigate);

    // Toasts de todas las vistas
    connect(m_cases, &CasesView::toast, this, &MainWindow::showToast);
    connect(m_run, &RunView::toast, this, &MainWindow::showToast);
    connect(m_bug, &BugView::toast, this, &MainWindow::showToast);
    connect(m_plan, &PlanView::toast, this, &MainWindow::showToast);
    connect(m_settings, &SettingsView::toast, this, &MainWindow::showToast);
    connect(m_history, &HistoryView::toast, this, &MainWindow::showToast);

    // Casos
    connect(m_cases, &CasesView::runRequested, this, [this](const QString& id) { m_ctx.run->start(id); navigate(Screen::Run); });
    connect(m_cases, &CasesView::captureRequested, m_ctx.evidence, &EvidenceService::captureForSelectedCase);
    connect(m_cases, &CasesView::historyRequested, this, [this](const QString& id) { m_history->showCase(id); navigate(Screen::Historial); });

    // Plan
    connect(m_plan, &PlanView::startPlanRequested, this, [this](const QStringList& ids, const QString& name) {
        m_ctx.run->startSequence(ids, name);
        navigate(Screen::Run);
        showToast(QStringLiteral("Plan \"%1\" iniciado · %2 casos").arg(name).arg(ids.size()), theme::Green);
    });

    // Ejecución
    connect(m_run, &RunView::captureRequested, m_ctx.evidence, &EvidenceService::captureForSelectedCase);
    connect(m_run, &RunView::reportBugRequested, this, [this]() { navigate(Screen::Bug); });
    connect(m_run, &RunView::finishRequested, this, &MainWindow::finishRun);

    // Historial
    connect(m_history, &HistoryView::openCaseRequested, this, [this](const QString& id) { m_ctx.cases->select(id); navigate(Screen::Casos); });

    // Bug
    connect(m_bug, &BugView::captureRequested, m_ctx.evidence, &EvidenceService::captureForSelectedCase);
    connect(m_bug, &BugView::cancelled, this, [this]() { navigate(Screen::Casos); });
    connect(m_bug, &BugView::submitted, this, [this](const QString&) { navigate(Screen::Casos); });

    // Evidencias
    connect(m_ctx.evidence, &EvidenceService::captured, this, [this](const QString&) {
        m_flash->flash();
        showToast(QStringLiteral("Captura guardada en %1").arg(m_ctx.settings->capture().folder), theme::Cyan);
    });
    connect(m_ctx.evidence, &EvidenceService::failed, this, [this](const QString& e) { showToast(e, theme::Amber); });
    connect(m_captureShortcut, &QShortcut::activated, m_ctx.evidence, &EvidenceService::captureForSelectedCase);
    connect(m_ctx.settings, &SettingsStore::captureChanged, this, &MainWindow::updateCaptureShortcut);
}

void MainWindow::finishRun() {
    const QString planId = m_ctx.run->planRunId();
    if (m_ctx.run->finish()) {
        showToast(QStringLiteral("Siguiente caso del plan · quedan %1").arg(m_ctx.run->queuedCount() + 1), theme::Green);
        return;
    }
    if (planId.isEmpty()) { navigate(Screen::Casos); return; }
    const PlanReport report = m_ctx.history->report(planId);
    m_history->showPlan(planId);
    navigate(Screen::Historial);
    const QString color = report.blocked ? theme::Amber : report.failed ? theme::Red : theme::Green;
    showToast(QStringLiteral("Plan terminado · %1 superados · %2 fallidos · %3 bloqueados").arg(report.passed).arg(report.failed).arg(report.blocked), color);
}

void MainWindow::updateCaptureShortcut() {
    m_captureShortcut->setKey(QKeySequence(m_ctx.settings->capture().shortcut));
}

void MainWindow::navigate(Screen s) {
    if (s == Screen::Bug) m_bug->loadDraft();
    m_stack->setCurrentIndex(static_cast<int>(s));
    m_sidebar->setActive(s);
}

void MainWindow::showToast(const QString& message, const QString& color) { m_toast->show(message, color); }

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    m_toast->reposition();
    if (m_flash->isVisible()) m_flash->setGeometry(centralWidget()->rect());
}

} // namespace qaflow
