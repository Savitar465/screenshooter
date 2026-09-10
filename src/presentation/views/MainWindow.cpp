#include "MainWindow.h"

#include "application/AppContext.h"
#include "core/models/RunHistory.h"   // label(Verdict)
#include "presentation/theme/Theme.h"
#include "presentation/views/BugView.h"
#include "presentation/views/CasesView.h"
#include "presentation/views/HistoryView.h"
#include "presentation/views/PlanView.h"
#include "presentation/views/RunView.h"
#include "presentation/views/SettingsDialog.h"
#include "presentation/views/Sidebar.h"
#include "presentation/views/StatusStrip.h"
#include "presentation/widgets/EvidenceActions.h"
#include "presentation/widgets/FlashOverlay.h"
#include "presentation/widgets/Toast.h"
#include "presentation/widgets/Ui.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QMenu>
#include <QMimeData>
#include <QMenuBar>
#include <QMessageBox>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>

namespace qaflow {

MainWindow::MainWindow(AppContext& ctx, QWidget* parent) : QMainWindow(parent), m_ctx(ctx) {
    setWindowTitle(QStringLiteral("QAflow"));
    setWindowIcon(ui::appIcon());
    setMinimumSize(1100, 720);
    resize(1360, 860);
    setAcceptDrops(true);

    auto* central = new QWidget;
    central->setObjectName(QStringLiteral("central"));
    // Rail de iconos + pantalla, y debajo la barra de estado a todo lo ancho.
    auto* rows = ui::vbox(central, 0, 0);
    auto* body = new QWidget;
    auto* h = ui::hbox(body, 0, 0);
    rows->addWidget(body, 1);

    m_sidebar = new Sidebar(*ctx.cases, *ctx.plan, *ctx.run, *ctx.history, *ctx.bugLedger);
    h->addWidget(m_sidebar);

    m_stack = new QStackedWidget;
    m_cases = new CasesView(*ctx.cases, *ctx.run, *ctx.history, *ctx.transfer, *ctx.bugLedger, *ctx.evidence);
    m_plan = new PlanView(*ctx.cases, *ctx.plan, ctx.publish);
    m_run = new RunView(*ctx.cases, *ctx.run, *ctx.settings, *ctx.evidence);
    m_history = new HistoryView(*ctx.cases, *ctx.history, ctx.publish, ctx.evidence, ctx.run);
    m_bug = new BugView(*ctx.cases, *ctx.settings, *ctx.bugs, *ctx.bugLedger, *ctx.evidence);
    m_stack->insertWidget(static_cast<int>(Screen::Casos), m_cases);
    m_stack->insertWidget(static_cast<int>(Screen::Plan), m_plan);
    m_stack->insertWidget(static_cast<int>(Screen::Run), m_run);
    m_stack->insertWidget(static_cast<int>(Screen::Historial), m_history);
    m_stack->insertWidget(static_cast<int>(Screen::Bug), m_bug);
    h->addWidget(m_stack, 1);
    m_status = new StatusStrip(*ctx.cases, *ctx.plan, *ctx.run, *ctx.history);
    rows->addWidget(m_status);
    setCentralWidget(central);

    m_toast = new Toast(body);            // sobre la pantalla, sin tapar la barra de estado
    m_flash = new FlashOverlay(central);   // el destello de captura sí cubre toda la ventana

    buildMenus();
    buildTray();
    wireSignals();
    updateShortcuts();
    updateActions();
    if (m_ctx.run->isRunning()) {
        // Ejecución restaurada de la sesión anterior.
        navigate(Screen::Run);
        const QString id = m_ctx.run->state().caseId;
        QTimer::singleShot(0, this, [this, id]() { showToast(tr("Ejecución de %1 recuperada de la sesión anterior").arg(id), theme::Blue); });
    } else {
        navigate(Screen::Casos);
    }
}

// ---- Menú y atajos ---------------------------------------------------------------------------

void MainWindow::buildMenus() {
    QMenuBar* bar = menuBar();
    bar->setNativeMenuBar(false);   // el QSS sólo se aplica a la barra propia

    // Archivo
    QMenu* file = bar->addMenu(tr("&Archivo"));
    auto* newCase = file->addAction(tr("&Nuevo caso"), QKeySequence::New, this, [this]() { m_ctx.cases->createCase(); navigate(Screen::Casos); });
    newCase->setObjectName(QStringLiteral("actNewCase"));
    file->addAction(tr("&Importar casos…"), QKeySequence::Open, this, [this]() { navigate(Screen::Casos); m_cases->importCases(); });
    QMenu* exportMenu = file->addMenu(tr("&Exportar casos"));
    exportMenu->addAction(tr("A &JSON…"), this, [this]() { m_cases->exportCases(CaseTransferService::Format::Json); });
    exportMenu->addAction(tr("A &CSV…"), this, [this]() { m_cases->exportCases(CaseTransferService::Format::Csv); });
    exportMenu->addAction(tr("A &Markdown…"), this, [this]() { m_cases->exportCases(CaseTransferService::Format::Markdown); });
    file->addSeparator();
    file->addAction(tr("Abrir carpeta de &datos"), this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctx.dataDir)); });
    file->addAction(tr("Abrir carpeta de &capturas"), this, [this]() { QDesktopServices::openUrl(QUrl::fromLocalFile(m_ctx.settings->capture().folder)); });
    file->addSeparator();
    // Los ajustes viven en su propia ventana, no en la pila de pantallas.
    auto* settings = file->addAction(tr("A&justes…"), QKeySequence(Qt::CTRL | Qt::Key_Comma), this, &MainWindow::openSettings);
    settings->setObjectName(QStringLiteral("actSettings"));
    settings->setMenuRole(QAction::PreferencesRole);
    file->addSeparator();
    auto* quit = file->addAction(tr("&Salir"), QKeySequence::Quit, this, &MainWindow::quitApplication);
    quit->setObjectName(QStringLiteral("actQuit"));
    quit->setMenuRole(QAction::QuitRole);

    // Editar
    QMenu* edit = bar->addMenu(tr("&Editar"));
    m_actUndo = edit->addAction(tr("&Deshacer"), QKeySequence::Undo, this, [this]() {
        if (m_ctx.cases->undo()) showToast(tr("Restaurado"), theme::Green);
    });
    m_actUndo->setObjectName(QStringLiteral("actUndo"));
    edit->addSeparator();
    auto* find = edit->addAction(tr("&Buscar caso"), QKeySequence::Find, this, [this]() { navigate(Screen::Casos); m_cases->focusSearch(); });
    find->setObjectName(QStringLiteral("actFind"));
    m_actDuplicate = edit->addAction(tr("D&uplicar caso"), QKeySequence(Qt::CTRL | Qt::Key_D), this, [this]() { navigate(Screen::Casos); m_cases->duplicateSelected(); });
    m_actDuplicate->setObjectName(QStringLiteral("actDuplicate"));
    m_actDelete = edit->addAction(tr("&Eliminar caso…"), QKeySequence(Qt::CTRL | Qt::Key_Delete), this, [this]() { navigate(Screen::Casos); m_cases->removeSelected(); });

    // Ver
    QMenu* view = bar->addMenu(tr("&Ver"));
    const std::pair<Screen, QString> screens[] = {
        {Screen::Casos, tr("&Casos de prueba")}, {Screen::Plan, tr("&Planes de pruebas")}, {Screen::Run, tr("&Ejecución")},
        {Screen::Historial, tr("&Historial")}, {Screen::Bug, tr("&Reportar bug")}};
    auto* screenGroup = new QActionGroup(this);
    int n = 1;
    for (const auto& [screen, label] : screens) {
        auto* a = view->addAction(label, QKeySequence(Qt::CTRL | (Qt::Key_0 + n++)), this, [this, screen]() { navigate(screen); });
        a->setCheckable(true);
        screenGroup->addAction(a);
        m_screenActions[screen] = a;
    }
    view->addSeparator();
    QMenu* themeMenu = view->addMenu(tr("&Tema"));
    m_themeGroup = new QActionGroup(this);
    const std::pair<AppTheme, QString> themes[] = {{AppTheme::Dark, tr("&Oscuro")}, {AppTheme::Light, tr("&Claro")}, {AppTheme::System, tr("Como el &sistema")}};
    for (const auto& [t, label] : themes) {
        auto* a = themeMenu->addAction(label, this, [this, t]() { m_ctx.settings->updateApp([t](AppSettings& s) { s.theme = t; }); });
        a->setCheckable(true);
        a->setChecked(m_ctx.settings->app().theme == t);
        m_themeGroup->addAction(a);
    }
    QMenu* langMenu = view->addMenu(tr("&Idioma"));
    m_languageGroup = new QActionGroup(this);
    const std::pair<AppLanguage, QString> languages[] = {{AppLanguage::System, tr("Como el s&istema")}, {AppLanguage::Spanish, QStringLiteral("Español")}, {AppLanguage::English, QStringLiteral("English")}};
    for (const auto& [l, label] : languages) {
        auto* a = langMenu->addAction(label, this, [this, l]() { m_ctx.settings->updateApp([l](AppSettings& s) { s.language = l; }); });
        a->setCheckable(true);
        a->setChecked(m_ctx.settings->app().language == l);
        m_languageGroup->addAction(a);
    }

    // Ejecución
    QMenu* runMenu = bar->addMenu(tr("E&jecución"));
    m_actRun = runMenu->addAction(tr("&Ejecutar caso seleccionado"), QKeySequence(Qt::Key_F5), this, [this]() {
        const QString id = m_ctx.cases->selectedId();
        if (id.isEmpty()) return;
        m_ctx.run->start(id);
        navigate(Screen::Run);
    });
    m_actRun->setObjectName(QStringLiteral("actRun"));
    m_actCapture = runMenu->addAction(tr("&Capturar pantalla"), this, [this]() { m_ctx.evidence->captureForSelectedCase(); });
    m_actCapture->setObjectName(QStringLiteral("actCapture"));
    m_actCapture->setShortcutContext(Qt::ApplicationShortcut);
    m_actRecord = runMenu->addAction(tr("&Grabar GIF"), this, [this]() { m_ctx.evidence->toggleRecording(); });
    m_actRecord->setObjectName(QStringLiteral("actRecord"));
    m_actRecord->setShortcutContext(Qt::ApplicationShortcut);
    m_actRecord->setVisible(m_ctx.evidence->canRecord());
    m_actAttach = runMenu->addAction(tr("Adjuntar &archivo…"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A), this, [this]() {
        m_ctx.evidence->attachFiles(evidence::pickFiles(this));
    });
    m_actAttach->setObjectName(QStringLiteral("actAttach"));
    m_actReportBug = runMenu->addAction(tr("&Reportar bug"), QKeySequence(Qt::CTRL | Qt::Key_B), this, [this]() { navigate(Screen::Bug); });
    runMenu->addSeparator();
    // Avanzar de paso sin volver a la ventana: son atajos de ámbito aplicación y, además,
    // main.cpp los registra en el sistema para que funcionen desde la aplicación que se prueba.
    m_actStepPass = runMenu->addAction(tr("&Pasa y siguiente"), this, [this]() { m_ctx.run->mark(StepResult::Pass); announceRunStep(); });
    m_actStepPass->setObjectName(QStringLiteral("actStepPass"));
    m_actStepFail = runMenu->addAction(tr("Fa&lla y siguiente"), this, [this]() { m_ctx.run->mark(StepResult::Fail); announceRunStep(); });
    m_actStepFail->setObjectName(QStringLiteral("actStepFail"));
    m_actStepBack = runMenu->addAction(tr("Paso &anterior"), this, [this]() { m_ctx.run->back(); announceRunStep(); });
    m_actStepBack->setObjectName(QStringLiteral("actStepBack"));
    for (QAction* a : {m_actStepPass, m_actStepFail, m_actStepBack}) a->setShortcutContext(Qt::ApplicationShortcut);

    // Ayuda
    QMenu* help = bar->addMenu(tr("A&yuda"));
    auto* about = help->addAction(tr("&Acerca de QAflow"), this, [this]() {
        QMessageBox::about(this, tr("Acerca de QAflow"),
                           tr("<b>QAflow</b> %1<br>Casos de prueba, planes, ejecución manual con evidencias y reporte de bugs.<br><br>"
                              "Datos: %2<br>Ajustes: %3")
                               .arg(QApplication::applicationVersion(), m_ctx.dataDir, m_ctx.settings->secretBackend()));
    });
    about->setMenuRole(QAction::AboutRole);
    help->addAction(tr("Atajos de &teclado"), this, [this]() {
        QMessageBox::information(this, tr("Atajos de teclado"),
                                 tr("<table cellspacing='6'>"
                                    "<tr><td><b>Ctrl+N</b></td><td>Nuevo caso</td></tr>"
                                    "<tr><td><b>Ctrl+F</b></td><td>Buscar caso</td></tr>"
                                    "<tr><td><b>Ctrl+D</b></td><td>Duplicar caso</td></tr>"
                                    "<tr><td><b>Ctrl+Z</b></td><td>Deshacer el último borrado</td></tr>"
                                    "<tr><td><b>F5</b></td><td>Ejecutar el caso seleccionado</td></tr>"
                                    "<tr><td><b>%1</b></td><td>Capturar pantalla (global si el sistema lo permite; vuelve a pulsar para cancelar la cuenta atrás)</td></tr>"
                                    "<tr><td><b>%2</b></td><td>Iniciar o detener la grabación de GIF</td></tr>"
                                    "<tr><td><b>Ctrl+Shift+A</b></td><td>Adjuntar archivos como evidencia (o arrástralos a la ventana)</td></tr>"
                                    "<tr><td><b>Clic en una miniatura</b></td><td>Abrir la evidencia a tamaño completo (← → navegan, Ctrl+E anota, Ctrl+C copia)</td></tr>"
                                    "<tr><td><b>%3</b></td><td>Pasa el paso actual y avanza al siguiente (global)</td></tr>"
                                    "<tr><td><b>%4</b></td><td>Falla el paso actual y avanza al siguiente (global)</td></tr>"
                                    "<tr><td><b>%5</b></td><td>Vuelve al paso anterior (global)</td></tr>"
                                    "<tr><td><b>Ctrl+B</b></td><td>Reportar bug</td></tr>"
                                    "<tr><td><b>Ctrl+1 … Ctrl+5</b></td><td>Cambiar de pantalla</td></tr>"
                                    "<tr><td><b>Ctrl+,</b></td><td>Abrir los ajustes</td></tr>"
                                    "<tr><td><b>P / F / B / S</b></td><td>Veredicto del paso en ejecución</td></tr>"
                                    "<tr><td><b>Retroceso</b></td><td>Volver al paso anterior</td></tr>"
                                    "<tr><td><b>Ctrl+Q</b></td><td>Salir</td></tr></table>")
                                     .arg(m_ctx.settings->capture().shortcut, m_ctx.settings->capture().recordShortcut,
                                          m_ctx.settings->runShortcuts().passAndNext, m_ctx.settings->runShortcuts().failAndNext,
                                          m_ctx.settings->runShortcuts().previous));
    });
}

void MainWindow::buildTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_tray = new QSystemTrayIcon(ui::appIcon(), this);
    m_tray->setToolTip(QStringLiteral("QAflow"));
    auto* menu = new QMenu(this);
    m_trayToggle = menu->addAction(tr("Ocultar QAflow"), this, [this]() {
        if (isVisible()) hide();
        else { show(); raise(); activateWindow(); }
        updateActions();
    });
    menu->addAction(tr("Capturar pantalla"), this, [this]() { m_ctx.evidence->captureForSelectedCase(); });
    if (m_ctx.evidence->canRecord()) {
        m_trayRecord = menu->addAction(tr("Grabar GIF"), this, [this]() { m_ctx.evidence->toggleRecording(); });
    }
    menu->addSeparator();
    menu->addAction(tr("Salir"), this, &MainWindow::quitApplication);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason != QSystemTrayIcon::Trigger && reason != QSystemTrayIcon::DoubleClick) return;
        if (isVisible() && !isMinimized()) hide();
        else { showNormal(); raise(); activateWindow(); }
        updateActions();
    });
    m_tray->show();
}

void MainWindow::updateActions() {
    m_actUndo->setEnabled(m_ctx.cases->canUndo());
    m_actUndo->setText(m_ctx.cases->canUndo() ? tr("&Deshacer «%1»").arg(m_ctx.cases->undoLabel()) : tr("&Deshacer"));
    const bool hasSelection = !m_ctx.cases->selectedId().isEmpty();
    m_actRun->setEnabled(hasSelection);
    m_actDuplicate->setEnabled(hasSelection);
    m_actDelete->setEnabled(hasSelection);
    m_actCapture->setEnabled(hasSelection);
    m_actRecord->setEnabled(hasSelection || m_ctx.evidence->isRecording());
    m_actAttach->setEnabled(hasSelection);
    m_actReportBug->setEnabled(hasSelection);
    const RunState& run = m_ctx.run->state();
    m_actStepPass->setEnabled(m_ctx.run->isRunning());
    m_actStepFail->setEnabled(m_ctx.run->isRunning());
    m_actStepBack->setEnabled(!run.caseId.isEmpty() && !run.results.isEmpty());
    if (m_trayToggle) m_trayToggle->setText(isVisible() ? tr("Ocultar QAflow") : tr("Mostrar QAflow"));
}

void MainWindow::openSettings() {
    if (!m_settings) {
        m_settings = new SettingsDialog(*m_ctx.settings, *m_ctx.bugs, m_ctx.hotkey, m_ctx.captureBackend, m_ctx.publish, this);
        connect(m_settings, &SettingsDialog::toast, this, &MainWindow::showToast);
    }
    m_settings->show();
    m_settings->raise();
    m_settings->activateWindow();
}

QWidget* MainWindow::settingsWindow() const { return m_settings && m_settings->isVisible() ? m_settings : nullptr; }

void MainWindow::quitApplication() {
    m_quitting = true;
    close();
    QApplication::quit();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!m_quitting && m_tray && m_tray->isVisible() && m_ctx.settings->app().closeToTray) {
        hide();
        if (!m_trayHintShown) {
            m_trayHintShown = true;
            m_tray->showMessage(QStringLiteral("QAflow"), tr("Sigue en la bandeja del sistema. Pulsa el icono para volver a abrir la ventana."), ui::appIcon(), 4000);
        }
        updateActions();
        e->ignore();
        return;
    }
    QMainWindow::closeEvent(e);
}

// ---- Señales ---------------------------------------------------------------------------------

void MainWindow::wireSignals() {
    connect(m_sidebar, &Sidebar::navigate, this, &MainWindow::navigate);
    connect(m_sidebar, &Sidebar::metricsRequested, this, &MainWindow::showMetrics);
    connect(m_status, &StatusStrip::navigate, this, &MainWindow::navigate);
    connect(m_status, &StatusStrip::metricsRequested, this, &MainWindow::showMetrics);

    // Toasts de todas las vistas
    connect(m_cases, &CasesView::toast, this, &MainWindow::showToast);
    connect(m_run, &RunView::toast, this, &MainWindow::showToast);
    connect(m_bug, &BugView::toast, this, &MainWindow::showToast);
    connect(m_plan, &PlanView::toast, this, &MainWindow::showToast);
    connect(m_history, &HistoryView::toast, this, &MainWindow::showToast);

    // Casos
    connect(m_cases, &CasesView::runRequested, this, [this](const QString& id) { m_ctx.run->start(id); navigate(Screen::Run); });
    connect(m_cases, &CasesView::historyRequested, this, [this](const QString& id) { m_history->showCase(id); navigate(Screen::Historial); });
    connect(m_cases, &CasesView::openRunRequested, this, [this](const QString& runId) {
        navigate(Screen::Historial);
        m_history->showRun(runId);
    });
    connect(m_cases, &CasesView::openJiraRequested, this, [this](const QString& key) {
        const TrackerSettings& t = m_ctx.settings->tracker();
        if (t.baseUrl().isEmpty()) { showToast(tr("Configura la URL del gestor en Ajustes"), theme::Amber); return; }
        QDesktopServices::openUrl(QUrl(t.issueUrl(key)));
    });
    connect(m_cases, &CasesView::openIssueRequested, this, [this](const QString& url) { if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url)); });
    connect(m_bug, &BugView::openIssueRequested, this, [this](const QString& url) { if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url)); });
    connect(m_ctx.cases, &TestCaseStore::selectionChanged, this, &MainWindow::updateActions);
    connect(m_ctx.cases, &TestCaseStore::casesChanged, this, &MainWindow::updateActions);
    // Deshacer borrados (caso, paso o captura) desde el aviso
    connect(m_ctx.cases, &TestCaseStore::undoAvailable, this, [this](const QString& label) {
        updateActions();
        m_toast->show(label, theme::Amber, tr("Deshacer"), [this]() {
            if (m_ctx.cases->undo()) showToast(tr("Restaurado"), theme::Green);
        });
    });

    // Plan
    connect(m_plan, &PlanView::startPlanRequested, this, [this](const QStringList& ids, const QString& name, const QString& planId) {
        m_ctx.run->startSequence(ids, name, planId);
        navigate(Screen::Run);
        showToast(tr("Ciclo de \"%1\" iniciado · %2 casos").arg(name).arg(ids.size()), theme::Green);
    });
    connect(m_plan, &PlanView::cycleReportRequested, this, [this](const QString& planRunId) { m_history->showPlan(planRunId); navigate(Screen::Historial); });
    // Activar o desactivar Zephyr en los ajustes cambia qué botones ofrecen los ciclos.
    connect(m_ctx.settings, &SettingsStore::trackerChanged, m_plan, &PlanView::refresh);
    connect(m_ctx.settings, &SettingsStore::trackerChanged, m_history, &HistoryView::refresh);
    connect(m_plan, &PlanView::openUrlRequested, this, [](const QString& url) { if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url)); });
    connect(m_history, &HistoryView::openUrlRequested, this, [](const QString& url) { if (!url.isEmpty()) QDesktopServices::openUrl(QUrl(url)); });
    connect(m_plan, &PlanView::openJiraRequested, this, [this](const QString& key) {
        const TrackerSettings& t = m_ctx.settings->tracker();
        if (t.baseUrl().isEmpty()) { showToast(tr("Configura la URL del gestor en Ajustes"), theme::Amber); return; }
        QDesktopServices::openUrl(QUrl(t.issueUrl(key)));
    });

    // Ejecución
    connect(m_run, &RunView::captureRequested, m_ctx.evidence, &EvidenceService::captureForSelectedCase);
    connect(m_run, &RunView::reportBugRequested, this, [this]() { navigate(Screen::Bug); });
    connect(m_run, &RunView::finishRequested, this, &MainWindow::finishRun);

    // Historial
    connect(m_history, &HistoryView::openCaseRequested, this, [this](const QString& id) { m_ctx.cases->select(id); navigate(Screen::Casos); });
    connect(m_history, &HistoryView::openJiraRequested, this, [this](const QString& key) {
        const TrackerSettings& t = m_ctx.settings->tracker();
        if (t.baseUrl().isEmpty()) { showToast(tr("Configura la URL del gestor en Ajustes"), theme::Amber); return; }
        QDesktopServices::openUrl(QUrl(t.issueUrl(key)));
    });

    // Bug
    connect(m_bug, &BugView::captureRequested, m_ctx.evidence, &EvidenceService::captureForSelectedCase);
    connect(m_bug, &BugView::cancelled, this, [this]() { navigate(Screen::Casos); });
    connect(m_bug, &BugView::submitted, this, [this](const QString&) { navigate(Screen::Casos); });
    // Cola offline: al arrancar con conexión configurada y bugs pendientes, se reintenta en silencio.
    if (!m_ctx.bugLedger->pending().isEmpty() && m_ctx.settings->tracker().connected) {
        QTimer::singleShot(1500, this, [this]() {
            m_ctx.bugs->retryPending([this](const BugReportService::RetryResult& r) {
                if (r.sent > 0) showToast(tr("Enviados %1 bugs que estaban pendientes: %2").arg(r.sent).arg(r.keys.join(QStringLiteral(", "))), theme::Green);
            });
        });
    }

    // Evidencias
    connect(m_ctx.evidence, &EvidenceService::captured, this, [this](const QString& path) {
        m_flash->flash();
        const bool gif = path.endsWith(QStringLiteral(".gif"), Qt::CaseInsensitive);
        showToast(gif ? tr("Grabación guardada en %1").arg(m_ctx.settings->capture().folder)
                      : tr("Captura guardada en %1").arg(m_ctx.settings->capture().folder), theme::Cyan);
    });
    connect(m_ctx.evidence, &EvidenceService::attached, this, [this](const QStringList& paths) {
        showToast(paths.size() == 1 ? tr("%1 adjuntado al caso").arg(QFileInfo(paths.first()).fileName())
                                    : tr("%1 archivos adjuntados al caso").arg(paths.size()), theme::Cyan);
    });
    connect(m_ctx.evidence, &EvidenceService::countdown, this, [this](int left) {
        if (left > 0) showToast(tr("Capturando en %1… (pulsa el atajo otra vez para cancelar)").arg(left), theme::Blue);
    });
    connect(m_ctx.evidence, &EvidenceService::recordingChanged, this, [this](bool on) {
        m_actRecord->setText(on ? tr("Detener la &grabación") : tr("&Grabar GIF"));
        if (m_trayRecord) m_trayRecord->setText(on ? tr("Detener la grabación") : tr("Grabar GIF"));
        if (on) showToast(tr("Grabando… pulsa Detener o %1 para terminar").arg(m_ctx.settings->capture().recordShortcut), theme::Violet);
        updateActions();
    });
    // Abrir el editor de anotaciones tras capturar, si así está configurado.
    connect(m_ctx.evidence, &EvidenceService::shotAdded, this, [this](const QString& caseId, int shotId, const QString& path) {
        if (!m_ctx.settings->capture().openEditor) return;
        Screenshot s;
        s.path = path;
        if (!s.isImage() || s.isAnimation() || !path.contains(QStringLiteral("/cap_"))) return;
        QTimer::singleShot(0, this, [this, caseId, shotId]() { evidence::annotate(this, *m_ctx.cases, *m_ctx.evidence, caseId, shotId); });
    });
    connect(m_ctx.evidence, &EvidenceService::failed, this, [this](const QString& e) { showToast(e, theme::Amber); });
    connect(m_ctx.settings, &SettingsStore::captureChanged, this, &MainWindow::updateShortcuts);
    connect(m_ctx.settings, &SettingsStore::runShortcutsChanged, this, &MainWindow::updateShortcuts);
    connect(m_ctx.run, &RunController::runChanged, this, &MainWindow::updateActions);

    // Fallos de guardado: cada store avisa; aquí se muestra con la opción de reintentar.
    connect(m_ctx.cases, &TestCaseStore::saveFailed, this, [this](const QString& what) { showSaveError(what, [this]() { return m_ctx.cases->save(); }); });
    connect(m_ctx.plan, &PlanStore::saveFailed, this, [this](const QString& what) { showSaveError(what, [this]() { return m_ctx.plan->save(); }); });
    connect(m_ctx.history, &RunHistoryStore::saveFailed, this, [this](const QString& what) { showSaveError(what, [this]() { return m_ctx.history->save(); }); });
    connect(m_ctx.bugLedger, &BugStore::saveFailed, this, [this](const QString& what) { showSaveError(what, [this]() { return m_ctx.bugLedger->save(); }); });
    connect(m_ctx.run, &RunController::saveFailed, this, [this](const QString& what) { showSaveError(what, [this]() { return m_ctx.run->persistSessionNow(); }); });
}

void MainWindow::showSaveError(const QString& what, const std::function<bool()>& retry) {
    m_toast->show(tr("No se pudieron guardar %1 en disco. Comprueba el espacio y los permisos de %2").arg(what, m_ctx.dataDir),
                  theme::Red, tr("Reintentar"), [this, retry]() {
                      if (retry()) showToast(tr("Guardado"), theme::Green);
                  });
}

void MainWindow::finishRun() {
    const QString planId = m_ctx.run->planRunId();
    if (m_ctx.run->finish()) {
        showToast(tr("Siguiente caso del plan · quedan %1").arg(m_ctx.run->queuedCount() + 1), theme::Green);
        return;
    }
    if (planId.isEmpty()) { navigate(Screen::Casos); return; }
    const PlanReport report = m_ctx.history->report(planId);
    m_history->showPlan(planId);
    navigate(Screen::Historial);
    const QString color = report.blocked ? theme::Amber : report.failed ? theme::Red : theme::Green;
    showToast(tr("Plan terminado · %1 superados · %2 fallidos · %3 bloqueados").arg(report.passed).arg(report.failed).arg(report.blocked), color);
}

void MainWindow::updateShortcuts() {
    m_actCapture->setShortcut(QKeySequence(m_ctx.settings->capture().shortcut));
    m_actRecord->setShortcut(QKeySequence(m_ctx.settings->capture().recordShortcut));
    const RunShortcuts& r = m_ctx.settings->runShortcuts();
    m_actStepPass->setShortcut(QKeySequence(r.passAndNext));
    m_actStepFail->setShortcut(QKeySequence(r.failAndNext));
    m_actStepBack->setShortcut(QKeySequence(r.previous));
}

void MainWindow::announceRunStep() {
    const RunState& r = m_ctx.run->state();
    const TestCase* c = m_ctx.cases->find(r.caseId);
    if (!c) return;
    const QString text = r.finished || r.idx >= c->steps.size()
                             ? tr("%1 · ejecución terminada · %2").arg(c->id, label(r.verdict()))
                             : tr("%1 · paso %2 de %3 · %4").arg(c->id).arg(r.idx + 1).arg(c->steps.size())
                                   .arg(ui::elide(c->steps[r.idx].action, 46));
    // Con la ventana al frente basta el aviso de siempre; si no, la bandeja lo enseña por encima
    // de la aplicación que se está probando.
    if (!isActiveWindow() && m_tray && m_tray->isVisible())
        m_tray->showMessage(QStringLiteral("QAflow"), text, ui::appIcon(), 2500);
    else
        showToast(text, theme::Blue);
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls() && !m_ctx.cases->selectedId().isEmpty()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    if (!e->mimeData()->hasUrls()) return;
    attachFiles(e->mimeData()->urls());
    e->acceptProposedAction();
}

void MainWindow::attachFiles(const QList<QUrl>& urls) {
    QStringList files;
    for (const QUrl& u : urls) if (u.isLocalFile()) files << u.toLocalFile();
    if (!files.isEmpty()) m_ctx.evidence->attachFiles(files);
}

void MainWindow::navigate(Screen s) {
    if (s == Screen::Bug) m_bug->loadDraft();
    m_current = s;
    m_stack->setCurrentIndex(static_cast<int>(s));
    m_sidebar->setActive(s);
    if (auto* a = m_screenActions.value(s)) a->setChecked(true);
}

void MainWindow::showToast(const QString& message, const QString& color) { m_toast->show(message, color); }

void MainWindow::showMetrics() {
    m_history->showMetrics();
    navigate(Screen::Historial);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    m_toast->reposition();
    if (m_flash->isVisible()) m_flash->setGeometry(centralWidget()->rect());
}

} // namespace qaflow
