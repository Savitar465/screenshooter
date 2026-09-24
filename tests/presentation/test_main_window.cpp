// MainWindow (presentation/views/MainWindow.h) con toda la capa de aplicación sobre repositorios
// en memoria y una captura de pantalla falsa. Se ejecuta con la plataforma "offscreen".
// Cubre: navegación (sidebar, menú, atajos), acciones de menú, teclas de la ejecución, filtros de
// la lista de casos, métricas y el aviso con «Reintentar» cuando falla el guardado.

#include "support/AppFixture.h"
#include "support/FakeScreenRecorder.h"

#include "application/AppContext.h"
#include "application/BugReportService.h"
#include "application/CaseTransferService.h"
#include "application/EvidenceService.h"
#include "presentation/views/BugDetailWindow.h"
#include "presentation/views/BugDialog.h"
#include "presentation/views/BugView.h"
#include "presentation/widgets/ShotCard.h"
#include "presentation/views/CasesView.h"
#include "presentation/views/IssuesView.h"
#include "presentation/views/CycleStartDialog.h"
#include "presentation/views/HistoryView.h"
#include "presentation/views/MainWindow.h"
#include "presentation/views/PlanView.h"
#include "presentation/views/JiraPublishDialog.h"
#include "presentation/views/ProjectSetupDialog.h"
#include "presentation/views/RevisionPublishDialog.h"
#include "presentation/views/RunView.h"
#include "presentation/views/Sidebar.h"
#include "presentation/views/StatusStrip.h"
#include "presentation/widgets/AnnotationEditor.h"
#include "presentation/widgets/ChoiceDialog.h"
#include "presentation/widgets/EvidencePreview.h"
#include "presentation/widgets/ImageViewer.h"
#include "presentation/widgets/Thumbnail.h"
#include "presentation/widgets/Toast.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFileInfo>
#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QTableWidget>
#include <QMimeData>
#include <QDropEvent>
#include <QPushButton>
#include <QProgressBar>
#include <QUrl>
#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;
using qaflow::testing::FakeScreenRecorder;

namespace {
class FakeScreenCapture : public IScreenCapture {
public:
    void capture(CaptureMode, Callback done) override {
        QImage img(4, 4, QImage::Format_ARGB32);
        img.fill(Qt::green);
        done(CaptureResult{true, img, {}});
    }
};

/// Fixture de aplicación + servicios que la ventana necesita + la ventana ya mostrada.
struct WindowFixture {
    AppFixture app;
    QTemporaryDir captures;
    std::shared_ptr<FakeScreenCapture> capture = std::make_shared<FakeScreenCapture>();
    std::shared_ptr<FakeScreenRecorder> recorder = std::make_shared<FakeScreenRecorder>();
    EvidenceService evidence{capture, app.store, app.run, app.settings};
    CaseTransferService transfer{app.store};
    AppContext ctx;
    std::unique_ptr<MainWindow> window;

    WindowFixture() {
        app.settings.updateCapture([&](CaptureSettings& c) { c.folder = captures.path(); c.delaySecs = 0; });
        evidence.setRecorder(recorder);
        ctx.cases = &app.store; ctx.plan = &app.plans; ctx.run = &app.run; ctx.history = &app.history;
        ctx.settings = &app.settings; ctx.bugs = &app.bugs; ctx.bugLedger = &app.bugLedger;
        ctx.evidence = &evidence; ctx.transfer = &transfer; ctx.dataDir = captures.path();
        ctx.publish = &app.publish;
        ctx.requirements = &app.requirements;
        ctx.issues = &app.issues;
        ctx.issuePublish = &app.issuePublish;
        ctx.records = &app.records;
        ctx.revisionPublish = &app.revisionPublish;
        window = std::make_unique<MainWindow>(ctx);
        window->show();
        QApplication::setActiveWindow(window.get());
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    }
    QAction* action(const char* name) const { return window->findChild<QAction*>(QString::fromLatin1(name)); }
    /// Etiqueta viva con ese nombre. Lo que se rehace en cada refresco (los pasos de la revisión) se
    /// borra con `deleteLater()`, así que primero se despacha lo pendiente y luego se busca.
    QLabel* liveLabel(const char* name) const {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return window->findChild<QLabel*>(QString::fromLatin1(name));
    }
    /// Botón vivo con ese nombre (las filas de GREQS y las tarjetas del tablero se rehacen en cada refresco).
    QPushButton* liveButton(const QString& name) const {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return window->findChild<QPushButton*>(name);
    }
    /// Pasa a la pestaña GREQS de la pantalla de issues, que lee la bandeja de GESREQ.
    void openGreqs() const {
        window->navigate(Screen::Issues);
        window->findChild<QPushButton*>(QStringLiteral("issuesTabGreqs"))->click();
    }
    /// La acción de la fila de un requerimiento en GREQS: seguir con su issue o empezar sus pruebas.
    QPushButton* greqAction(const QString& id) const { return liveButton(QStringLiteral("greqAction-%1").arg(id)); }
    /// Lo que dice la fila de un requerimiento en GREQS de su issue («SIN ISSUE», «IS-0001 · Riesgos»…).
    QString greqStatus(const QString& id) const {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* l = window->findChild<QLabel*>(QStringLiteral("greqStatus-%1").arg(id));
        return l ? l->text() : QString();
    }
    QPushButton* nav(Screen s) const { return window->findChild<QPushButton*>(QStringLiteral("nav-%1").arg(static_cast<int>(s))); }
    /// Insignia del botón del rail (progreso de la ejecución, bugs pendientes…).
    QLabel* badge(Screen s) const { return window->findChild<QLabel*>(QStringLiteral("badge-%1").arg(static_cast<int>(s))); }
    /// Texto de un bloque de la barra de estado.
    QString statusText(const char* name) const {
        auto* b = window->findChild<QPushButton*>(QString::fromLatin1(name));
        QStringList parts;
        for (auto* l : b->findChildren<QLabel*>()) if (!l->text().isEmpty()) parts << l->text();
        return parts.join(QStringLiteral(" "));
    }
    /// Filas visibles de la lista de casos.
    int visibleCaseRows() const {
        int n = 0;
        for (auto* b : window->findChildren<QPushButton*>())
            if (b->property("role").toString() == QStringLiteral("row") && b->isVisible() && b->window() == window.get()) ++n;
        return n;
    }
    /// El parte de bug abierto, si lo hay: desde que se reporta en su propia ventana, los campos
    /// del formulario están ahí y no en la pantalla.
    BugDialog* bugDialog() const { return window->findChild<BugDialog*>(); }
    /// Arrancar un ciclo pregunta antes en qué ambiente se prueba: responde al diálogo y acepta.
    /// Falso si no hay ninguno abierto (el ciclo no llegó a ofrecerse).
    bool answerCycleDialog(const QString& environment = QStringLiteral("QA")) const {
        auto* dialog = window->findChild<CycleStartDialog*>();
        if (!dialog) return false;
        auto* combo = dialog->findChild<QComboBox*>(QStringLiteral("cycleStartEnvironment"));
        auto* accept = dialog->findChild<QPushButton*>(QStringLiteral("cycleStartAccept"));
        if (!combo || !accept) return false;
        combo->setCurrentText(environment);
        accept->click();
        return true;
    }
    Toast* toast() const { return window->findChild<Toast*>(); }
    QString toastText() const {
        Toast* t = toast();
        return t ? t->findChild<QLabel*>()->text() : QString();
    }
};
} // namespace

class MainWindowTest : public QObject {
    Q_OBJECT
private slots:
    void opensOnCasesAndSidebarNavigates() {
        WindowFixture f;
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Casos));
        QVERIFY(f.nav(Screen::Historial));
        QTest::mouseClick(f.nav(Screen::Historial), Qt::LeftButton);
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Historial));
        QTest::mouseClick(f.nav(Screen::Bug), Qt::LeftButton);
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Bug));
    }

    void fileMenuOpensSettingsInItsOwnWindow() {
        WindowFixture f;
        QVERIFY(!f.window->settingsWindow());
        QCOMPARE(f.action("actSettings")->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_Comma));
        f.action("actSettings")->trigger();
        QWidget* dialog = f.window->settingsWindow();
        QVERIFY(dialog);
        QVERIFY(dialog->isWindow());
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Casos));   // la pantalla no cambia
        // Los ajustes se guardan al momento: el tema elegido en la ventana llega al store.
        auto* theme = dialog->findChild<QComboBox*>(QStringLiteral("settingsTheme"));
        theme->setCurrentIndex(theme->findData(static_cast<int>(AppTheme::Light)));
        QCOMPARE(static_cast<int>(f.app.settings.app().theme), static_cast<int>(AppTheme::Light));
        dialog->findChild<QPushButton*>(QStringLiteral("settingsClose"))->click();
        QVERIFY(!f.window->settingsWindow());
    }

    void railBadgeAndStatusStripFollowTheRun() {
        WindowFixture f;
        QVERIFY(!f.badge(Screen::Run)->isVisible());
        QVERIFY(f.statusText("statusRun").contains(QStringLiteral("Sin ejecución")));
        QVERIFY(f.nav(Screen::Casos)->toolTip().contains(QStringLiteral("Ctrl+1")));

        f.app.run.start(QStringLiteral("TC-101"));
        QVERIFY(f.badge(Screen::Run)->isVisible());
        QCOMPARE(f.badge(Screen::Run)->text(), QStringLiteral("0/%1").arg(f.app.run.totalSteps()));
        QVERIFY(f.statusText("statusRun").contains(QStringLiteral("TC-101")));
        QVERIFY(f.nav(Screen::Run)->toolTip().contains(QStringLiteral("TC-101")));

        // El bloque de la ejecución lleva a su pantalla.
        QTest::mouseClick(f.window->findChild<QPushButton*>(QStringLiteral("statusRun")), Qt::LeftButton);
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Run));
    }

    void statusStripShowsOnlyTheRunningPlanOrStandaloneCase() {
        WindowFixture f;
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("statusPlan")));
        auto* progress = f.window->findChild<QProgressBar*>(QStringLiteral("statusRunProgress"));
        QVERIFY(progress);
        QVERIFY(!progress->isVisible());

        const QString planId = f.app.plans.activeId();
        f.app.run.startSequence({QStringLiteral("TC-101"), QStringLiteral("TC-102")}, QStringLiteral("Plan en ejecución"), planId);
        QVERIFY(f.statusText("statusRun").contains(QStringLiteral("Plan en ejecución")));
        QVERIFY(!f.statusText("statusRun").contains(QStringLiteral("TC-101")));
        QVERIFY(progress->isVisible());
        QCOMPARE(progress->value(), 0);

        // Abrir otro plan no cambia el indicador de la ejecución real.
        f.app.plans.createPlan(QStringLiteral("Otro plan"));
        QVERIFY(f.statusText("statusRun").contains(QStringLiteral("Plan en ejecución")));
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);
        f.app.run.finish();
        QCOMPARE(progress->value(), 50);
        QTest::mouseClick(f.window->findChild<QPushButton*>(QStringLiteral("statusRun")), Qt::LeftButton);
        QCOMPARE(f.window->currentScreen(), Screen::Run);

        f.app.run.abandon();
        QVERIFY(f.statusText("statusRun").contains(QStringLiteral("Sin ejecución")));
        QVERIFY(!progress->isVisible());
        f.app.run.start(QStringLiteral("TC-101"));
        QVERIFY(f.statusText("statusRun").contains(QStringLiteral("TC-101")));
        QVERIFY(!f.statusText("statusRun").contains(QStringLiteral("Plan en ejecución")));
        QVERIFY(!progress->isVisible());
    }

    void menuActionsHaveStandardShortcuts() {
        WindowFixture f;
        QCOMPARE(f.action("actNewCase")->shortcut(), QKeySequence(QKeySequence::New));
        QCOMPARE(f.action("actFind")->shortcut(), QKeySequence(QKeySequence::Find));
        QCOMPARE(f.action("actUndo")->shortcut(), QKeySequence(QKeySequence::Undo));
        QCOMPARE(f.action("actQuit")->shortcut(), QKeySequence(QKeySequence::Quit));
        QCOMPARE(f.action("actRun")->shortcut(), QKeySequence(Qt::Key_F5));
        QCOMPARE(f.action("actCapture")->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+S")));   // el de Ajustes
        QCOMPARE(f.action("actRecord")->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+G")));
        QCOMPARE(f.action("actAttach")->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
        f.app.settings.updateCapture([](CaptureSettings& c) { c.shortcut = QStringLiteral("F9"); c.recordShortcut = QStringLiteral("F10"); });
        QCOMPARE(f.action("actCapture")->shortcut(), QKeySequence(Qt::Key_F9));
        QCOMPARE(f.action("actRecord")->shortcut(), QKeySequence(Qt::Key_F10));
    }

    void recordActionTogglesTheRecorderAndAttachesTheGif() {
        WindowFixture f;
        const QString id = f.app.store.selectedId();
        f.app.run.start(id);   // la evidencia es de la ejecución
        QVERIFY(f.action("actRecord")->isVisible());
        f.action("actRecord")->trigger();
        QVERIFY(f.evidence.isRecording());
        QVERIFY(f.action("actRecord")->text().contains(QStringLiteral("Detener")));
        f.action("actRecord")->trigger();
        QVERIFY(!f.evidence.isRecording());
        QTRY_COMPARE(f.app.store.find(id)->shots.size(), 1);
        QVERIFY(f.app.store.find(id)->shots[0].isAnimation());
        QTRY_VERIFY(f.toastText().contains(QStringLiteral("Grabaci")));
    }

    void droppingFilesAttachesThemToTheRunningCase() {
        WindowFixture f;
        const QString id = f.app.store.selectedId();
        f.app.run.start(id);
        const QString log = f.captures.filePath(QStringLiteral("app.log"));
        { QFile file(log); file.open(QIODevice::WriteOnly); file.write("x"); }
        QSignalSpy failed(&f.evidence, &EvidenceService::failed);
        f.window->attachFiles({QUrl::fromLocalFile(log), QUrl(QStringLiteral("https://example.com/no-local"))});
        QVERIFY2(failed.isEmpty(), qPrintable(failed.isEmpty() ? QString() : failed.first().at(0).toString()));
        QCOMPARE(f.app.store.find(id)->shots.size(), 1);
        QVERIFY(f.app.store.find(id)->shots[0].fileName.endsWith(QStringLiteral("_app.log")));
        QVERIFY(!f.app.store.find(id)->shots[0].isImage());
        QTRY_VERIFY(f.toastText().contains(QStringLiteral("app.log")));
    }

    // Desde el caso se salta a los resultados de una de sus ejecuciones.
    void clickingARunInTheCaseOpensItsResults() {
        WindowFixture f;
        const QString id = f.app.store.selectedId();
        f.app.run.start(id);
        f.action("actCapture")->trigger();
        QTRY_COMPARE(f.app.store.find(id)->shots.size(), 1);
        while (f.app.run.isRunning()) f.app.run.mark(StepResult::Pass);
        f.app.run.finish();
        const QString runId = f.app.history.runsForCase(id).first().id;

        f.window->navigate(Screen::Casos);
        auto* row = f.window->findChild<QPushButton*>(QStringLiteral("caseRun-%1").arg(runId));
        QVERIFY(row);
        QVERIFY2(row->findChild<QLabel*>() != nullptr, "la fila resume la ejecución");
        row->click();

        // Aterriza en el historial, en los resultados de esa ejecución y con su evidencia.
        QCOMPARE(f.window->currentScreen(), Screen::Historial);
        auto* history = f.window->findChild<HistoryView*>();
        QVERIFY(history);
        auto visibleThumb = [&]() {
            for (auto* t : f.window->findChildren<Thumbnail*>()) if (t->isVisible()) return true;
            return false;
        };
        QTRY_VERIFY(visibleThumb());
    }

    // La evidencia de una ejecución se ve en su ficha del historial; un clic en una captura abre
    // directamente el editor de anotaciones.
    void clickingAThumbnailOpensTheAnnotationEditor() {
        WindowFixture f;
        const QString id = f.app.store.selectedId();
        f.app.run.start(id);
        f.action("actCapture")->trigger();
        QTRY_COMPARE(f.app.store.find(id)->shots.size(), 1);
        while (f.app.run.isRunning()) f.app.run.mark(StepResult::Pass);
        f.app.run.finish();

        // La vista del historial se construye al entrar en ella, así que primero se navega.
        f.window->navigate(Screen::Historial);
        auto* history = f.window->findChild<HistoryView*>();
        QVERIFY(history);
        history->showRun(f.app.history.runsForCase(id).first().id);
        // Reportar bug también crea tarjetas (ocultas): hay que esperar a la miniatura visible.
        auto visibleThumb = [&]() -> Thumbnail* {
            for (auto* t : f.window->findChildren<Thumbnail*>()) if (t->isVisible()) return t;
            return nullptr;
        };
        QTRY_VERIFY(visibleThumb() != nullptr);
        // El editor es modal (exec): se cierra desde el bucle de eventos que abre.
        bool opened = false;
        QTimer::singleShot(0, [&opened]() {
            auto* editor = qobject_cast<AnnotationEditor*>(QApplication::activeModalWidget());
            opened = editor != nullptr;
            if (editor) editor->reject();
        });
        QTest::mouseClick(visibleThumb(), Qt::LeftButton);
        QVERIFY(opened);
        QVERIFY(f.window->findChild<ImageViewer*>() == nullptr);
    }

    void captureCountdownShowsAToastAndCanBeCancelled() {
        WindowFixture f;
        f.app.run.start(f.app.store.selectedId());
        f.app.settings.updateCapture([](CaptureSettings& c) { c.delaySecs = 5; });
        f.action("actCapture")->trigger();
        QTRY_VERIFY(f.toastText().contains(QStringLiteral("Capturando en 5")));
        f.action("actCapture")->trigger();   // cancela
        QVERIFY(!f.evidence.isCountingDown());
        QTRY_VERIFY(f.toastText().contains(QStringLiteral("cancelada")));
        QVERIFY(f.app.store.selected()->shots.isEmpty());
    }

    void newCaseActionCreatesAndSelectsACase() {
        WindowFixture f;
        f.window->navigate(Screen::Plan);
        const int before = f.app.store.cases().size();
        f.action("actNewCase")->trigger();
        QCOMPARE(f.app.store.cases().size(), before + 1);
        QCOMPARE(f.app.store.selectedId(), QStringLiteral("TC-108"));
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Casos));
    }

    void ctrlFFocusesTheSearchBoxAndFiltersTheList() {
        WindowFixture f;
        f.window->navigate(Screen::Historial);
        QTest::keyClick(f.window.get(), Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Casos));
        auto* search = f.window->findChild<QLineEdit*>(QStringLiteral("caseSearch"));
        QVERIFY(search);
        QTRY_VERIFY(search->hasFocus());
        QCOMPARE(f.visibleCaseRows(), 7);
        QTest::keyClicks(search, QStringLiteral("tarjeta"));   // sólo ASCII: QTest::keyClicks no admite tildes
        QTRY_COMPARE(f.visibleCaseRows(), 1);                  // las filas nuevas se muestran al procesar eventos
        search->clear();
        QTRY_COMPARE(f.visibleCaseRows(), 7);
    }

    void runScreenAcceptsVerdictKeys() {
        WindowFixture f;
        f.action("actRun")->trigger();   // TC-104 (seleccionado al cargar)
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Run));
        QVERIFY(f.app.run.isRunning());
        QTest::keyClick(f.window.get(), Qt::Key_P);
        QCOMPARE(f.app.run.state().markedCount(), 1);
        QTest::keyClick(f.window.get(), Qt::Key_F);
        QCOMPARE(f.app.run.state().markedCount(), 2);
        QCOMPARE(static_cast<int>(f.app.run.state().results[1].result), static_cast<int>(StepResult::Fail));
        // Retroceso y Alt+→ sólo mueven el paso en pantalla: los veredictos se quedan donde están.
        QTest::keyClick(f.window.get(), Qt::Key_Backspace);
        QCOMPARE(f.app.run.state().idx, 1);
        QCOMPARE(f.app.run.state().markedCount(), 2);
        QTest::keyClick(f.window.get(), Qt::Key_Right, Qt::AltModifier);
        QCOMPARE(f.app.run.state().idx, 2);
    }

    /// Los pasos de la ejecución son navegables: un clic en su número (pestaña «Paso») o en su tarjeta
    /// (pestaña «Pasos») lleva a ese paso, marcado o no, y la tarjeta vuelve a la ficha del paso.
    void clickingAStepOfTheRunListGoesToIt() {
        WindowFixture f;
        f.action("actRun")->trigger();   // TC-104, 4 pasos
        f.action("actStepPass")->trigger();
        QCOMPARE(f.app.run.state().idx, 1);

        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* chip = f.window->findChild<QPushButton*>(QStringLiteral("stepChip3"));
        QVERIFY(chip);
        QTRY_VERIFY(chip->isVisible());   // hasta que el inspector se coloca
        QTest::mouseClick(chip, Qt::LeftButton);
        QCOMPARE(f.app.run.state().idx, 2);
        QCOMPARE(f.app.run.state().markedCount(), 1);

        auto* stepTab = f.window->findChild<QPushButton*>(QStringLiteral("runStepTab"));
        auto* stepsTab = f.window->findChild<QPushButton*>(QStringLiteral("runStepsTab"));
        QVERIFY(stepTab->isChecked());
        QCOMPARE(stepsTab->text(), QStringLiteral("Pasos · 4"));
        stepsTab->click();
        // Las tarjetas se rehacen en cada refresco: se despachan las viejas, que esperan su borrado.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* fourth = f.window->findChild<QFrame*>(QStringLiteral("stepCard4"));
        QVERIFY(fourth);
        QVERIFY(fourth->isVisible());
        QTest::mouseClick(fourth, Qt::LeftButton);
        QCOMPARE(f.app.run.state().idx, 3);
        QCOMPARE(f.app.run.state().markedCount(), 1);   // saltar no marca nada
        QVERIFY(stepTab->isChecked());                  // y se ve la ficha del paso elegido
        stepsTab->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        auto* first = f.window->findChild<QFrame*>(QStringLiteral("stepCard1"));
        QVERIFY(first);
        QTest::mouseClick(first, Qt::LeftButton);       // volver a uno ya marcado
        QCOMPARE(f.app.run.state().idx, 0);
        QVERIFY(f.app.run.state().isMarked(0));
    }

    /// En un ciclo, la pestaña «Casos» lista los del plan con su estado y lleva de uno a otro: el que
    /// se deja queda en pausa con lo marcado. Con un caso suelto la pestaña no está.
    void theCasesTabOfTheRunMovesBetweenTheCasesOfTheCycle() {
        WindowFixture f;
        f.action("actRun")->trigger();   // un caso suelto
        auto* casesTab = f.window->findChild<QPushButton*>(QStringLiteral("runCasesTab"));
        QVERIFY(casesTab);
        QVERIFY(casesTab->isHidden());

        f.app.run.startSequence({QStringLiteral("TC-104"), QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"));
        f.window->navigate(Screen::Run);
        f.app.run.mark(StepResult::Pass);
        QTRY_VERIFY(casesTab->isVisible());
        QCOMPARE(casesTab->text(), QStringLiteral("Casos · 0/3"));
        casesTab->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* target = f.window->findChild<QFrame*>(QStringLiteral("caseCard-TC-107"));
        QVERIFY(target);
        QTRY_VERIFY(target->isVisible());
        QTest::mouseClick(target, Qt::LeftButton);
        QTRY_COMPARE(f.app.run.state().caseId, QStringLiteral("TC-107"));
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("runStepTab"))->isChecked());

        // El que se dejó sigue en la lista, en pausa y con su paso marcado; y se vuelve a él.
        casesTab->click();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* back = f.window->findChild<QFrame*>(QStringLiteral("caseCard-TC-104"));
        QVERIFY(back);
        QVERIFY(!back->findChildren<QLabel*>().isEmpty());
        bool paused = false;
        for (auto* l : back->findChildren<QLabel*>()) paused |= l->text() == QStringLiteral("EN PAUSA · 1/4");
        QVERIFY(paused);
        QTest::mouseClick(back, Qt::LeftButton);
        QTRY_COMPARE(f.app.run.state().caseId, QStringLiteral("TC-104"));
        QCOMPARE(f.app.run.state().markedCount(), 1);
    }

    /// La pantalla de bugs es el libro del proyecto: lista lo reportado con su estado, filtra por
    /// estado y por texto, trae del gestor lo que QAflow creó allí y abre la ficha de cada uno.
    void theBugScreenListsTheProjectBugsAndBringsThemFromTheTracker() {
        WindowFixture f;
        IssueLink open;
        open.key = QStringLiteral("SHOP-143"); open.title = QStringLiteral("El cupón no descuenta");
        open.caseId = QStringLiteral("TC-104"); open.step = 2; open.tracker = QStringLiteral("Jira");
        open.status = QStringLiteral("In Progress"); open.createdAt = QDateTime::currentDateTime().addDays(-2);
        f.app.bugLedger.recordIssue(open);
        IssueLink closed;
        closed.key = QStringLiteral("SHOP-90"); closed.title = QStringLiteral("Login sin mensaje");
        closed.tracker = QStringLiteral("Jira"); closed.status = QStringLiteral("Done"); closed.resolved = true;
        f.app.bugLedger.recordIssue(closed);

        f.window->navigate(Screen::Bug);
        const auto rows = [&f]() {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            QStringList keys;
            for (auto* b : f.window->findChildren<QPushButton*>())
                if (b->objectName().startsWith(QStringLiteral("bugRow-")) && b->window() == f.window.get())
                    keys << b->objectName().mid(7);
            return keys;
        };
        QCOMPARE(rows(), (QStringList{QStringLiteral("SHOP-90"), QStringLiteral("SHOP-143")}));   // el último, primero

        f.window->findChild<QPushButton*>(QStringLiteral("bugsFilterOpen"))->click();
        QCOMPARE(rows(), QStringList{QStringLiteral("SHOP-143")});
        f.window->findChild<QPushButton*>(QStringLiteral("bugsFilterAll"))->click();
        f.window->findChild<QLineEdit*>(QStringLiteral("bugsSearch"))->setText(QStringLiteral("login"));
        QCOMPARE(rows(), QStringList{QStringLiteral("SHOP-90")});
        f.window->findChild<QLineEdit*>(QStringLiteral("bugsSearch"))->clear();

        // Traer de Jira: el que ya está se actualiza y el que no, entra con su caso.
        TrackerIssueInfo known;
        known.key = QStringLiteral("SHOP-143"); known.title = open.title;
        known.status = QStringLiteral("Done"); known.resolved = true;
        TrackerIssueInfo foreign;
        foreign.key = QStringLiteral("SHOP-155"); foreign.title = QStringLiteral("Error 500 al pagar");
        foreign.status = QStringLiteral("To Do"); foreign.labels = {QStringLiteral("qaflow"), QStringLiteral("TC-101")};
        f.app.tracker->issuesToReturn = {known, foreign};
        f.window->findChild<QPushButton*>(QStringLiteral("bugsImport"))->click();
        QTRY_COMPARE(f.app.bugLedger.issues().size(), 3);
        QVERIFY(f.app.bugLedger.findIssue(QStringLiteral("SHOP-143"))->resolved);
        QCOMPARE(f.app.bugLedger.findIssue(QStringLiteral("SHOP-155"))->caseId, QStringLiteral("TC-101"));
        QVERIFY(rows().contains(QStringLiteral("SHOP-155")));

        // Y la fila abre la ficha del bug en su ventana.
        f.window->findChild<QPushButton*>(QStringLiteral("bugRow-SHOP-155"))->click();
        auto* detail = f.window->findChild<BugDetailWindow*>();
        QVERIFY(detail);
        QCOMPARE(detail->bugKey(), QStringLiteral("SHOP-155"));
    }

    /// La lista no se pinta entera de golpe: se alarga al deslizar hasta abajo y, cuando se acaba lo
    /// que hay en el libro, le pide al gestor la página siguiente.
    void theBugListGrowsAsYouScrollAndAsksTheTrackerForMore() {
        WindowFixture f;
        for (int i = 0; i < 30; ++i) {
            IssueLink l;
            l.key = QStringLiteral("SHOP-%1").arg(100 + i);
            l.title = QStringLiteral("Bug número %1").arg(i);
            l.tracker = QStringLiteral("Jira");
            l.createdAt = QDateTime::currentDateTime().addSecs(-i * 60);
            f.app.bugLedger.recordIssue(l);
        }
        f.window->navigate(Screen::Bug);
        const auto rows = [&f]() {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            int n = 0;
            for (auto* b : f.window->findChildren<QPushButton*>())
                if (b->objectName().startsWith(QStringLiteral("bugRow-")) && b->window() == f.window.get()) ++n;
            return n;
        };
        auto* scroll = f.window->findChild<BugView*>()->findChild<QScrollArea*>();
        QVERIFY(scroll);
        const auto toBottom = [scroll]() {
            QScrollBar* bar = scroll->verticalScrollBar();
            bar->setValue(0);
            QCoreApplication::processEvents();
            bar->setValue(bar->maximum());
            QCoreApplication::processEvents();
        };
        QTRY_VERIFY(rows() > 0);
        QVERIFY2(rows() < 30, qPrintable(QString::number(rows())));   // sólo la primera página
        toBottom();
        QTRY_COMPARE(rows(), 30);                                     // deslizar trae el resto

        // Y con el libro agotado, la página siguiente se le pide al gestor.
        QList<TrackerIssueInfo> remote;
        for (int i = 0; i < BugReportService::kImportPage + 5; ++i) {
            TrackerIssueInfo info;
            info.key = QStringLiteral("SHOP-%1").arg(500 + i);
            info.title = QStringLiteral("remoto %1").arg(i);
            info.labels = {QStringLiteral("qaflow")};
            remote << info;
        }
        f.app.tracker->issuesToReturn = remote;
        f.window->findChild<QPushButton*>(QStringLiteral("bugsImport"))->click();
        QTRY_COMPARE(f.app.bugLedger.issues().size(), 30 + BugReportService::kImportPage);
        QCOMPARE(f.app.tracker->issueSearchStarts, QList<int>{0});

        for (int i = 0; i < 6 && f.app.tracker->issueSearchStarts.size() < 2; ++i) toBottom();
        QTRY_COMPARE(f.app.tracker->issueSearchStarts, (QList<int>{0, BugReportService::kImportPage}));
        QCOMPARE(f.app.bugLedger.issues().size(), 30 + remote.size());
    }

    /// Reportar no es una pantalla: el botón de la lista abre el parte en su ventana.
    void theBugScreenOpensTheReportInItsOwnWindow() {
        WindowFixture f;
        f.window->navigate(Screen::Bug);
        QVERIFY(!f.bugDialog());
        f.window->findChild<QPushButton*>(QStringLiteral("bugsCreate"))->click();
        QVERIFY(f.bugDialog());
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Bug));
        f.bugDialog()->reject();
    }

    /// Un paso fallido se reporta sin esperar a que termine la ejecución, y el parte llega con ese
    /// paso enlazado; un paso bloqueado además pide el bug como bloqueante.
    void aFailedStepCanBeReportedWithoutClosingTheRun() {
        WindowFixture f;
        f.action("actRun")->trigger();   // TC-104, 4 pasos
        QTest::keyClick(f.window.get(), Qt::Key_F);
        QCOMPARE(f.app.run.state().idx, 1);   // sigue abierta por el paso siguiente
        auto* report = f.window->findChild<QPushButton*>(QStringLiteral("runReportBug"));
        QVERIFY(report);
        QTRY_VERIFY(report->isVisible() && report->width() > 0);   // hasta que la columna se coloca
        QTest::mouseClick(report, Qt::LeftButton);
        // El parte se abre en su ventana: la ejecución se queda en pantalla, detrás.
        QVERIFY(f.bugDialog());
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Run));
        QCOMPARE(f.window->findChild<QLineEdit*>(QStringLiteral("bugTitle"))->text().contains(QStringLiteral("paso 1")), true);
        f.bugDialog()->reject();              // se deja el parte y se sigue probando
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        QVERIFY(f.app.run.isRunning());       // y se puede seguir probando el resto
        QTest::keyClick(f.window.get(), Qt::Key_B);
        QCOMPARE(f.app.run.state().markedCount(), 2);
        QVERIFY(f.app.run.isRunning());
        QCOMPARE(report->accessibleName(), QStringLiteral("Reportar bug bloqueante"));   // es un icono: lo dice su nombre
        QTest::mouseClick(report, Qt::LeftButton);
        auto* severity = f.window->findChild<QComboBox*>(QStringLiteral("bugSeverity"));
        QVERIFY(severity);
        QCOMPARE(severity->currentData().toString(), QStringLiteral("Bloqueante"));
    }

    /// El parte arranca con una copia de la última captura de la ejecución, no con todas; quitarla
    /// o añadir otras no toca la evidencia de la ejecución, y lo que se crea lleva las del parte.
    void theBugReportHasItsOwnAttachments() {
        WindowFixture f;
        f.action("actRun")->trigger();
        const QString id = f.app.store.selectedId();
        f.action("actCapture")->trigger();
        f.action("actCapture")->trigger();
        QTRY_COMPARE(f.app.store.find(id)->shots.size(), 2);
        const QList<Screenshot> runShots = f.app.store.find(id)->shots;

        f.window->reportBug();
        BugDialog* dialog = f.bugDialog();
        QVERIFY(dialog);
        QCOMPARE(dialog->attachments().size(), 1);
        const Screenshot copy = dialog->attachments().first();
        QVERIFY(copy.path != runShots.last().path);
        QCOMPARE(QFileInfo(copy.path).size(), QFileInfo(runShots.last().path).size());

        // Quitarla del parte no la quita de la ejecución.
        auto* card = dialog->findChild<ShotCard*>();
        QVERIFY(card);
        emit card->removeRequested(card->shot().id);
        QVERIFY(dialog->attachments().isEmpty());
        QVERIFY(!QFile::exists(copy.path));
        QCOMPARE(f.app.store.find(id)->shots.size(), 2);
        for (const auto& s : runShots) QVERIFY(QFile::exists(s.path));

        // Capturar desde el parte añade al parte, no a la ejecución.
        auto* capture = dialog->findChild<QPushButton*>(QStringLiteral("bugCapture"));
        QVERIFY(capture);
        capture->click();
        QTRY_COMPARE(dialog->attachments().size(), 1);
        QCOMPARE(f.app.store.find(id)->shots.size(), 2);
        // Y el atajo de captura, con el parte abierto, también va al parte.
        QVERIFY(dialog->actions().contains(f.action("actCapture")));
        f.action("actCapture")->trigger();
        QTRY_COMPARE(dialog->attachments().size(), 2);
        QCOMPARE(f.app.store.find(id)->shots.size(), 2);
        const QString captured = dialog->attachments().first().path;
        const QString byShortcut = dialog->attachments().last().path;

        // Cancelar el parte borra sus adjuntos.
        dialog->reject();
        QVERIFY(!QFile::exists(captured));
        QVERIFY(!QFile::exists(byShortcut));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        // Sin parte, el atajo vuelve a capturar para la ejecución.
        f.action("actCapture")->trigger();
        QTRY_COMPARE(f.app.store.find(id)->shots.size(), 3);
        for (const auto& s : runShots) QVERIFY(QFile::exists(s.path));
    }

    /// El bug que se crea pertenece al paso del que se reportó: el formulario lo trae puesto, se
    /// puede cambiar y el paso llega al libro de bugs (de ahí sale el defecto de Zephyr).
    void aReportedBugBelongsToItsStep() {
        WindowFixture f;
        f.action("actRun")->trigger();   // TC-104, 4 pasos
        QTest::keyClick(f.window.get(), Qt::Key_P);
        f.app.run.setNote(QStringLiteral("el total no cambia"));   // el parte necesita resultado actual
        QTest::keyClick(f.window.get(), Qt::Key_F);                // falla el paso 2

        auto* report = f.window->findChild<QPushButton*>(QStringLiteral("runReportBug"));
        QVERIFY(report);
        QTRY_VERIFY(report->isVisible() && report->width() > 0);
        QTest::mouseClick(report, Qt::LeftButton);

        auto* step = f.window->findChild<QComboBox*>(QStringLiteral("bugStep"));
        QVERIFY(step);
        QCOMPARE(step->count(), 5);                        // «Todo el caso» + los cuatro pasos
        QCOMPARE(step->currentData().toInt(), 2);          // el paso que falló, ya elegido
        step->setCurrentIndex(step->findData(3));          // y se puede corregir a mano

        auto* title = f.window->findChild<QLineEdit*>(QStringLiteral("bugTitle"));
        QVERIFY(title && !title->text().isEmpty());
        auto* send = f.window->findChild<QPushButton*>(QStringLiteral("bugSubmit"));
        QVERIFY(send);
        QTRY_VERIFY(send->isVisible() && send->width() > 0);
        QTest::mouseClick(send, Qt::LeftButton);
        QTRY_COMPARE(f.app.bugLedger.issues().size(), 1);
        const IssueLink& link = f.app.bugLedger.issues().first();
        QCOMPARE(link.caseId, QStringLiteral("TC-104"));
        QCOMPARE(link.step, 3);

        // Y la tarjeta del paso 3 de la ejecución lo enseña. (La lista se rehace en cada refresco:
        // se espera a que las tarjetas viejas, en cola de borrado, desaparezcan.)
        f.window->navigate(Screen::Run);
        const QString key = link.key;
        const auto bugShownOnThirdStep = [&f, &key]() {
            const auto cards = f.window->findChildren<QFrame*>(QStringLiteral("stepCard3"));
            if (cards.size() != 1) return false;
            for (auto* l : cards.first()->findChildren<QLabel*>()) if (l->text() == key) return true;
            return false;
        };
        QTRY_VERIFY(bugShownOnThirdStep());
    }

    /// Los atajos de la ejecución (los mismos que main.cpp registra en el sistema) avanzan y
    /// retroceden de paso desde el menú, sin pasar por la pantalla.
    void runStepActionsFollowTheSettings() {
        WindowFixture f;
        QCOMPARE(f.action("actStepPass")->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+P")));
        QCOMPARE(f.action("actStepFail")->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+F")));
        QCOMPARE(f.action("actStepBack")->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+A")));
        QCOMPARE(f.action("actStepNext")->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+D")));
        QVERIFY(!f.action("actStepPass")->isEnabled());   // sin ejecución no hacen nada
        QVERIFY(!f.action("actStepBack")->isEnabled());

        f.action("actRun")->trigger();   // TC-104
        QVERIFY(f.action("actStepPass")->isEnabled());
        f.action("actStepPass")->trigger();
        QCOMPARE(f.app.run.state().markedCount(), 1);
        QCOMPARE(static_cast<int>(f.app.run.state().results[0].result), static_cast<int>(StepResult::Pass));
        QCOMPARE(f.app.run.state().idx, 1);
        f.action("actStepFail")->trigger();
        QCOMPARE(static_cast<int>(f.app.run.state().results[1].result), static_cast<int>(StepResult::Fail));
        f.action("actStepBack")->trigger();
        QCOMPARE(f.app.run.state().markedCount(), 2);
        QCOMPARE(f.app.run.state().idx, 1);
        f.action("actStepNext")->trigger();
        QCOMPARE(f.app.run.state().idx, 2);

        f.app.settings.updateRunShortcuts([](RunShortcuts& r) { r.passAndNext = QStringLiteral("F8"); });
        QCOMPARE(f.action("actStepPass")->shortcut(), QKeySequence(Qt::Key_F8));
    }

    void captureActionAttachesScreenshotToTheRunningCase() {
        WindowFixture f;
        const QString id = f.app.store.selectedId();
        f.app.run.start(id);
        f.action("actCapture")->trigger();
        QTRY_COMPARE(f.app.store.find(id)->shots.size(), 1);
        QVERIFY(QFile::exists(f.app.store.find(id)->shots[0].path));
        QTRY_VERIFY(f.toast()->isVisible());
    }

    /// Sin ejecución, la pantalla sólo enseña su mensaje: ni paneles laterales ni barra flotante.
    void runScreenWithoutRunShowsOnlyItsMessage() {
        WindowFixture f;
        f.window->navigate(Screen::Run);
        QVERIFY(!f.app.run.isRunning());
        QVERIFY(!f.window->findChild<QFrame*>(QStringLiteral("shotBar"))->isVisible());
        QVERIFY(!f.window->findChild<QFrame*>(QStringLiteral("casePanel"))->isVisible());
        QVERIFY(!f.window->findChild<QFrame*>(QStringLiteral("filmPanel"))->isVisible());
        QVERIFY(!f.window->findChild<EvidencePreview*>(QStringLiteral("evidencePreview"))->isVisible());
        // Y sin ejecución no hay nada que enfocar.
        QTest::keyClick(f.window.get(), Qt::Key_F11);
        QVERIFY(!f.window->findChild<RunView*>()->focusMode());
    }

    /// El modo foco deja la evidencia a toda la ventana: se van el inspector, la tira y el marco de
    /// la ventana, y el mando del pie sigue marcando pasos. Esc, el botón o salir de la pantalla vuelven.
    void focusModeLeavesTheEvidenceAloneAndKeepsTheVerdictsAtHand() {
        WindowFixture f;
        f.action("actRun")->trigger();   // TC-104
        auto* run = f.window->findChild<RunView*>();
        QVERIFY(run);
        auto* inspector = f.window->findChild<QFrame*>(QStringLiteral("casePanel"));
        auto* film = f.window->findChild<QFrame*>(QStringLiteral("filmPanel"));
        auto* bar = f.window->findChild<QFrame*>(QStringLiteral("focusBar"));
        auto* sidebar = f.window->findChild<Sidebar*>();
        auto* status = f.window->findChild<StatusStrip*>();
        QVERIFY(inspector->isVisible() && film->isVisible() && !bar->isVisible());

        QTest::keyClick(f.window.get(), Qt::Key_F11);
        QVERIFY(run->focusMode());
        QVERIFY(!inspector->isVisible() && !film->isVisible() && bar->isVisible());
        QVERIFY(!sidebar->isVisible() && !status->isVisible());
        QVERIFY(f.window->findChild<EvidencePreview*>(QStringLiteral("evidencePreview"))->isVisible());

        // Se sigue probando sin salir: la P marca y el mando dice en qué paso se está.
        QTest::keyClick(f.window.get(), Qt::Key_P);
        QCOMPARE(f.app.run.state().markedCount(), 1);
        QVERIFY(run->focusMode());
        QVERIFY(bar->findChild<QLabel*>()->text().contains(QStringLiteral("2")));

        QTest::keyClick(f.window.get(), Qt::Key_Escape);
        QVERIFY(!run->focusMode());
        QVERIFY(inspector->isVisible() && film->isVisible() && !bar->isVisible());
        QVERIFY(sidebar->isVisible() && status->isVisible());

        // El botón del visor también entra, y cambiar de pantalla saca del modo: el marco vuelve.
        f.window->findChild<QPushButton*>(QStringLiteral("runFocus"))->click();
        QVERIFY(run->focusMode());
        f.window->navigate(Screen::Casos);
        QVERIFY(!run->focusMode());
        QVERIFY(sidebar->isVisible() && status->isVisible());
    }

    /// El visor de la pantalla de ejecución abre la última captura, la barra la reasigna de paso
    /// y las flechas recorren el carrete.
    void runScreenPreviewFollowsTheEvidence() {
        WindowFixture f;
        f.action("actRun")->trigger();   // TC-104
        const QString id = f.app.run.state().caseId;
        f.action("actCapture")->trigger();
        QTRY_COMPARE(f.app.store.find(id)->shots.size(), 1);
        f.action("actCapture")->trigger();
        QTRY_COMPARE(f.app.store.find(id)->shots.size(), 2);
        const QList<Screenshot> shots = f.app.store.find(id)->shots;

        auto* preview = f.window->findChild<EvidencePreview*>(QStringLiteral("evidencePreview"));
        QVERIFY(preview);
        QCOMPARE(preview->shotId(), shots[1].id);   // la captura recién hecha se abre sola
        auto* assign = f.window->findChild<QComboBox*>(QStringLiteral("assignStep"));
        QCOMPARE(assign->currentData().toInt(), shots[1].step);   // paso en ejecución
        assign->setCurrentIndex(assign->findData(3));
        QCOMPARE(f.app.store.find(id)->shots[1].step, 3);

        QTest::mouseClick(f.window->findChild<QPushButton*>(QStringLiteral("shotPrev")), Qt::LeftButton);
        QCOMPARE(preview->shotId(), shots[0].id);
        QTest::mouseClick(f.window->findChild<QPushButton*>(QStringLiteral("shotNext")), Qt::LeftButton);
        QCOMPARE(preview->shotId(), shots[1].id);
    }

    /// Con varias capturas, la última cae fuera de la parte visible de la tira: debe traerse a la vista.
    void newScreenshotScrollsIntoViewInTheFilmStrip() {
        WindowFixture f;
        f.action("actRun")->trigger();   // TC-104
        const QString id = f.app.run.state().caseId;
        for (int i = 1; i <= 6; ++i) {
            f.action("actCapture")->trigger();
            QTRY_COMPARE(f.app.store.find(id)->shots.size(), i);
        }
        auto* scroll = f.window->findChild<QScrollArea*>(QStringLiteral("filmScroll"));
        QVERIFY(scroll);
        QScrollBar* bar = scroll->horizontalScrollBar();   // la tira va en horizontal, bajo el visor
        QTRY_VERIFY(bar->maximum() > 0);                 // hay más capturas de las que caben
        QTRY_COMPARE(bar->value(), bar->maximum());      // desplazado hasta la última
    }

    void undoActionFollowsTheStore() {
        WindowFixture f;
        QVERIFY(!f.action("actUndo")->isEnabled());
        f.app.store.removeCase(QStringLiteral("TC-107"));
        QVERIFY(f.action("actUndo")->isEnabled());
        QVERIFY(f.action("actUndo")->text().contains(QStringLiteral("TC-107")));
        QVERIFY(f.toast()->isVisible());   // aviso con «Deshacer»
        f.action("actUndo")->trigger();
        QVERIFY(f.app.store.find(QStringLiteral("TC-107")));
        QVERIFY(!f.action("actUndo")->isEnabled());
    }

    void metricsCardOpensHistoryMetrics() {
        WindowFixture f;
        auto* card = f.window->findChild<QPushButton*>(QStringLiteral("metricCard"));
        QVERIFY(card);
        QTest::mouseClick(card, Qt::LeftButton);
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Historial));
        bool found = false;
        for (auto* l : f.window->findChildren<QLabel*>()) if (l->text() == QStringLiteral("TASA DE ÉXITO POR SUITE")) found = true;
        QVERIFY(found);
    }

    void failedSaveShowsPersistentToastWithRetry() {
        WindowFixture f;
        f.app.repo->failWrites = true;
        f.app.store.createCase();
        QTRY_VERIFY_WITH_TIMEOUT(f.toast()->isVisible() && f.toastText().contains(QStringLiteral("casos")), 2000);   // guardado diferido (400 ms)
        QVERIFY(f.app.store.hasUnsavedChanges());
        auto* retry = f.toast()->findChild<QPushButton*>();
        QVERIFY(retry && retry->isVisible());
        f.app.repo->failWrites = false;
        QTest::mouseClick(retry, Qt::LeftButton);
        QVERIFY(!f.app.store.hasUnsavedChanges());
        QTRY_VERIFY(f.toastText().contains(QStringLiteral("Guardado")));
    }

    // El campo "Asignado a" busca las personas en el gestor según se escribe, sin pisar el texto.
    void bugAssigneeSearchesPeopleInTheTracker() {
        WindowFixture f;
        f.app.tracker->searchesAssignees = true;
        f.app.tracker->assigneesToReturn = {Assignee{QStringLiteral("aperez"), QStringLiteral("Ana Pérez")},
                                            Assignee{QStringLiteral("apedro"), QStringLiteral("Pedro Antón")}};
        f.window->reportBug();
        auto* assignee = f.window->findChild<QComboBox*>(QStringLiteral("bugAssignee"));
        QVERIFY(assignee);
        QCOMPARE(assignee->count(), 0);
        QTest::keyClicks(assignee->lineEdit(), QStringLiteral("an"));
        QTRY_COMPARE(assignee->count(), 2);
        QCOMPARE(assignee->itemText(0), QStringLiteral("Ana Pérez"));
        QCOMPARE(assignee->itemData(0).toString(), QStringLiteral("aperez"));   // el id que espera Jira Server
        QCOMPARE(assignee->lineEdit()->text(), QStringLiteral("an"));           // lo escrito sigue intacto
        QCOMPARE(f.app.tracker->assigneeQueries.last(), QStringLiteral("an"));
        QCOMPARE(f.app.tracker->assigneeQueries.size(), 1);                     // una sola llamada para dos letras
    }

    // El informe del plan y el detalle de la ejecución dicen con qué Jira y qué Zephyr está enlazado
    // cada caso, y en qué ciclo de Zephyr acabaron esos resultados.
    void thePlanReportShowsWhatEachCaseIsLinkedTo() {
        WindowFixture f;
        // Con Zephyr activo la cabecera lleva además el botón de publicar: es la fila más apretada.
        f.app.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; s.url = QStringLiteral("https://jira.acme.com"); s.project = QStringLiteral("SHOP"); });
        const QString planRunId = f.app.history.startPlan(QStringLiteral("Regresión Sprint 14 · candidata de release"), {QStringLiteral("TC-101")});
        RunRecord rec;
        rec.caseId = QStringLiteral("TC-101");
        rec.caseTitle = QStringLiteral("Iniciar sesión");
        rec.planRunId = planRunId;
        rec.testKey = QStringLiteral("SHOP-42");   // el Test que se creó para esta ejecución al publicarla
        rec.verdict = Verdict::Superado;
        rec.startedAt = QDateTime::currentDateTime().addSecs(-300);
        rec.finishedAt = QDateTime::currentDateTime();
        rec.plannedSteps = 1;
        rec.steps = {RunRecordStep{QStringLiteral("Entrar"), {}, QStringLiteral("Entra"), StepResult::Pass, {}, 30}};
        const RunRecord saved = f.app.history.addRun(rec);
        f.app.history.finishPlan(planRunId);
        f.app.history.markPublished(planRunId, QStringLiteral("77"));

        // A lo ancho de una pantalla normal, no de la máxima: la cabecera del informe lleva tres
        // botones y es donde se apretaba la línea de la publicación.
        f.window->resize(760, 700);
        f.window->navigate(Screen::Historial);
        auto* history = f.window->findChild<HistoryView*>();
        QVERIFY(history);
        history->showPlan(planRunId);
        QTest::qWait(50);
        // La historia del caso (de los datos de ejemplo) y su Test de Zephyr, uno al lado del otro.
        auto* jira = f.window->findChild<QPushButton*>(QStringLiteral("linkJira"));
        auto* test = f.window->findChild<QPushButton*>(QStringLiteral("linkTest"));
        QVERIFY(jira);
        QVERIFY(test);
        QCOMPARE(jira->text(), QStringLiteral("Historia SHOP-3"));
        QCOMPARE(test->text(), QStringLiteral("Test SHOP-42"));
        // Y dónde se publicó el ciclo.
        auto* published = f.window->findChild<QLabel*>(QStringLiteral("planPublished"));
        QVERIFY(published);
        QVERIFY2(published->text().contains(QStringLiteral("77")), qPrintable(published->text()));
        // Y el salto al ciclo en Jira: la búsqueda de ejecuciones de ese ciclo.
        QSignalSpy opened(history, &HistoryView::openUrlRequested);
        auto* cycleBtn = f.window->findChild<QPushButton*>(QStringLiteral("openZephyrCycle"));
        QVERIFY(cycleBtn);
        QTest::mouseClick(cycleBtn, Qt::LeftButton);
        QCOMPARE(opened.count(), 1);
        QVERIFY2(opened.first().first().toString().startsWith(QStringLiteral("https://jira.acme.com/secure/enav/#?query=")), qPrintable(opened.first().first().toString()));
        // Y se ve entero: apretado contra los botones de la cabecera se quedaba en "Publicado el 0".
        QVERIFY2(published->width() >= published->sizeHint().width(),
                 qPrintable(QStringLiteral("ancho %1 < necesario %2 · texto: %3")
                                .arg(published->width()).arg(published->sizeHint().width()).arg(published->text())));

        // El detalle de la ejecución dice lo mismo: sus enlaces y el ciclo en el que acabó.
        history->showRun(saved.id);
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("linkTest")));
        auto* cycle = f.window->findChild<QLabel*>(QStringLiteral("runZephyrCycle"));
        QVERIFY(cycle);
        QVERIFY2(cycle->text().contains(QStringLiteral("77")), qPrintable(cycle->text()));
    }

    // En la pantalla de planes, un ciclo publicado enseña el Test de cada ejecución y ofrece
    // actualizar el ciclo de Zephyr; uno sin publicar, publicarlo.
    void projectSettingsHaveTheirOwnSection() {
        WindowFixture f;
        f.window->openSettings();
        auto* dialog = f.window->settingsWindow();
        QVERIFY(dialog);
        auto* project = dialog->findChild<QWidget*>(QStringLiteral("projectSettingsSection"));
        auto* general = dialog->findChild<QWidget*>(QStringLiteral("generalTrackerSettings"));
        auto* code = dialog->findChild<QLineEdit*>(QStringLiteral("settingsJiraProject"));
        QVERIFY(project && general && code);
        QVERIFY(project->isAncestorOf(code));
        QVERIFY(!general->isAncestorOf(code));
        const QString url = f.app.settings.tracker().url;
        code->selectAll();
        QTest::keyClicks(code, "NEW");
        QCOMPARE(f.app.settings.tracker().project, QStringLiteral("NEW"));
        QCOMPARE(f.app.settings.tracker().url, url);
        f.app.settings.updateTracker([](TrackerSettings& t) { t.kind = TrackerKind::GitHub; });
        QVERIFY(project->isHidden());
        QVERIFY(!general->isHidden());
    }

    void navbarRunsCasesAndPlansAndPreventsReplacingAnActiveRun() {
        WindowFixture f;
        auto* targets = f.window->findChild<QComboBox*>(QStringLiteral("runTargetSelector"));
        auto* run = f.window->findChild<QPushButton*>(QStringLiteral("navbarRun"));
        auto* stop = f.window->findChild<QPushButton*>(QStringLiteral("navbarStop"));
        auto* finish = f.window->findChild<QPushButton*>(QStringLiteral("navbarFinish"));
        QVERIFY(targets && run && stop && finish);
        QVERIFY(run->isEnabled());
        QVERIFY(!stop->isEnabled());
        targets->setCurrentIndex(targets->findData(QStringLiteral("case:TC-103")));
        run->click();
        QCOMPARE(f.app.run.state().caseId, QStringLiteral("TC-103"));
        QVERIFY(f.app.run.planRunId().isEmpty());
        QVERIFY(!run->isEnabled());
        QVERIFY(!targets->isEnabled());
        QVERIFY(stop->isEnabled());
        f.app.run.mark(StepResult::Pass);
        QVERIFY(!finish->isHidden());
        finish->click();
        QVERIFY(f.app.run.state().caseId.isEmpty());
        QVERIFY(run->isEnabled());
        f.window->navigate(Screen::Plan);
        QCOMPARE(targets->currentData().toString(), QStringLiteral("plan:") + f.app.plans.activeId());
        run->click();
        // El ciclo no arranca hasta decir en qué ambiente se prueba, que es dato suyo.
        QVERIFY(f.app.run.planRunId().isEmpty());
        QVERIFY(f.answerCycleDialog(QStringLiteral("Staging")));
        QVERIFY(!f.app.run.planRunId().isEmpty());
        QCOMPARE(f.app.history.findPlan(f.app.run.planRunId())->environment, QStringLiteral("Staging"));
        QCOMPARE(f.app.run.state().caseId, f.app.plans.orderedCaseIds().first());
        QCOMPARE(f.app.run.queuedCount(), f.app.plans.orderedCaseIds().size() - 1);
        QCOMPARE(f.window->currentScreen(), Screen::Run);
    }

    // El plan se ejecuta y se le añaden casos desde su propia pantalla, que es donde se compone.
    void thePlanScreenRunsThePlanAndCreatesItsCases() {
        WindowFixture f;
        f.window->navigate(Screen::Plan);
        auto* run = f.window->findChild<QPushButton*>(QStringLiteral("planRun"));
        auto* newCase = f.window->findChild<QPushButton*>(QStringLiteral("planNewCase"));
        QVERIFY(run && newCase);
        QVERIFY(run->isEnabled());

        // Un caso nuevo nace dentro del plan y se abre para escribir sus pasos.
        const qsizetype before = f.app.plans.orderedCaseIds().size();
        newCase->click();
        const QString caseId = f.app.store.selectedId();
        QCOMPARE(f.app.plans.orderedCaseIds().size(), before + 1);
        QCOMPARE(f.app.plans.orderedCaseIds().last(), caseId);
        QCOMPARE(f.window->currentScreen(), Screen::Casos);

        // Y «Ejecutar plan» arranca su ciclo (tras decir el ambiente) y lleva a la ejecución.
        f.window->navigate(Screen::Plan);
        run->click();
        QVERIFY(f.answerCycleDialog());
        QCOMPARE(f.window->currentScreen(), Screen::Run);
        QVERIFY(!f.app.run.planRunId().isEmpty());
        QCOMPARE(f.app.run.state().caseId, f.app.plans.orderedCaseIds().first());

        // Con una ejecución en curso no se arranca otra.
        f.window->navigate(Screen::Plan);
        run->click();
        QVERIFY(f.window->findChild<Toast*>());

        // Un plan vacío no se puede ejecutar.
        f.app.run.abandon();
        const QString empty = f.app.plans.createPlan(QStringLiteral("Vacío"));
        f.app.plans.setActive(empty);
        QVERIFY(!run->isEnabled());
    }

    void planCaseSearchPaginatesAndPreservesSelection() {
        AppFixture f;
        QList<TestCase> incoming;
        for (int i = 0; i < 21; ++i) {
            TestCase c;
            c.id = QStringLiteral("PAGE-%1").arg(i, 2, 10, QLatin1Char('0'));
            c.title = QStringLiteral("Caso paginado %1").arg(i);
            c.tags = {QStringLiteral("paginacion")};
            incoming.append(c);
        }
        f.store.mergeCases(incoming);
        PlanView view(f.store, f.plans);
        view.show();
        auto* search = view.findChild<QLineEdit*>(QStringLiteral("planCaseSearch"));
        auto* next = view.findChild<QPushButton*>(QStringLiteral("availableNextPage"));
        auto* previous = view.findChild<QPushButton*>(QStringLiteral("availablePreviousPage"));
        auto* summary = view.findChild<QLabel*>(QStringLiteral("availablePageSummary"));
        QVERIFY(search && next && previous && summary);
        auto flush = [] { QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); };
        search->setText(QStringLiteral("  PAGINACION  "));
        flush();
        QVERIFY(summary->text().contains(QStringLiteral("1–10 de 21")));
        QVERIFY(!previous->isEnabled());
        next->click();
        next->click();
        flush();
        QVERIFY(summary->text().contains(QStringLiteral("21–21 de 21")));
        QVERIFY(!next->isEnabled());
        auto* add = view.findChild<QPushButton*>(QStringLiteral("addPlanCase-PAGE-20"));
        QVERIFY(add);
        add->click();
        flush();
        QVERIFY(f.plans.active()->contains(QStringLiteral("PAGE-20")));
        QVERIFY(summary->text().contains(QStringLiteral("11–20 de 20")));
        search->setText(QStringLiteral("page-20"));
        flush();
        QVERIFY(summary->text().contains(QStringLiteral("0–0 de 0")));
        auto* remove = view.findChild<QPushButton*>(QStringLiteral("removePlanCase-PAGE-20"));
        QVERIFY(remove);
        remove->click();
        flush();
        QVERIFY(!f.plans.active()->contains(QStringLiteral("PAGE-20")));
        QVERIFY(summary->text().contains(QStringLiteral("1–1 de 1")));
        search->clear();
        f.plans.selectAll();
        flush();
        auto* selectedNext = view.findChild<QPushButton*>(QStringLiteral("inPlanNextPage"));
        QVERIFY(selectedNext && selectedNext->isEnabled());
        const auto selected = f.plans.orderedCaseIds();
        selectedNext->click();
        search->setText(QStringLiteral("sin coincidencias"));
        QCOMPARE(f.plans.orderedCaseIds(), selected);
        auto* selectedSummary = view.findChild<QLabel*>(QStringLiteral("inPlanPageSummary"));
        QVERIFY(selectedSummary->text().contains(QStringLiteral("0–0 de 0")));
        search->clear();
        QVERIFY(selectedSummary->text().contains(QStringLiteral("1–10")));
    }

    void thePlanScreenShowsTheZephyrTestOfEachPublishedRun() {
        WindowFixture f;
        f.app.settings.updateTracker([](TrackerSettings& s) { s.zephyr = true; });
        const QString planId = f.app.plans.activeId();
        // Ciclo 1, publicado: una ejecución con Test y otra que se quedó sin él.
        f.app.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"), planId);
        const QString published = f.app.run.planRunId();
        f.app.run.mark(StepResult::Pass);
        f.app.run.finish();
        f.app.run.mark(StepResult::Pass);
        f.window->finishRun();
        const QList<RunRecord> runs = f.app.history.runsForPlan(published);
        QCOMPARE(runs.size(), 2);
        f.app.history.assignTestKeys({{runs[0].id, QStringLiteral("SHOP-77")}});
        f.app.history.markPublished(published, QStringLiteral("77"));
        // Ciclo 2, terminado y sin publicar.
        f.app.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Regresión"), planId);
        const QString unpublished = f.app.run.planRunId();
        f.app.run.mark(StepResult::Pass);
        f.window->finishRun();

        f.window->navigate(Screen::Plan);
        QTest::qWait(50);
        auto* history = f.window->findChild<QPushButton*>(QStringLiteral("cycleHistoryToggle"));
        QVERIFY(history);
        QVERIFY(!history->isChecked());
        auto* current = f.window->findChild<QFrame*>(QStringLiteral("currentPlanCycle"));
        QVERIFY(current);
        QVERIFY(current->isHidden());
        QTest::mouseClick(history, Qt::LeftButton);
        auto* test = f.window->findChild<QPushButton*>(QStringLiteral("cycleTest-%1").arg(runs[0].id));
        QVERIFY(test);
        QCOMPARE(test->text(), QStringLiteral("Test SHOP-77"));
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("cycleTest-%1").arg(runs[1].id)));   // sin Test: sin chip
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("updateZephyr-%1").arg(published)));
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("publishZephyr-%1").arg(published)));
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("publishZephyr-%1").arg(unpublished)));
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("updateZephyr-%1").arg(unpublished)));

        // Sin Zephyr en los ajustes no se ofrece ni publicar ni actualizar, pero los Tests se siguen viendo.
        f.app.settings.updateTracker([](TrackerSettings& s) { s.zephyr = false; });
        QTest::qWait(50);
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("cycleTest-%1").arg(runs[0].id)));
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("updateZephyr-%1").arg(published)));
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("publishZephyr-%1").arg(unpublished)));
    }

    void thePlanCycleListsTheResultOfEachCase() {
        WindowFixture f;
        const QString planId = f.app.plans.activeId();
        // Un ciclo con un caso superado y otro fallado: es lo que hay que poder distinguir de un vistazo.
        f.app.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"), planId);
        const QString cycle = f.app.run.planRunId();
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);
        f.app.run.finish();
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Fail);
        f.window->finishRun();

        f.window->navigate(Screen::Plan);
        QTest::qWait(50);
        QTest::mouseClick(f.window->findChild<QPushButton*>(QStringLiteral("cycleHistoryToggle")), Qt::LeftButton);
        auto* results = f.window->findChild<QPushButton*>(QStringLiteral("cycleResults-%1").arg(cycle));
        QVERIFY(results);
        // Plegado, el ciclo sigue siendo un resumen; desplegado, dice qué dio cada caso.
        QVERIFY(!f.window->findChild<QFrame*>(QStringLiteral("cycleResultRow-%1-TC-103").arg(cycle)));
        QTest::mouseClick(results, Qt::LeftButton);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(f.window->findChild<QFrame*>(QStringLiteral("cycleResultRow-%1-TC-103").arg(cycle)));
        auto* broken = f.window->findChild<QFrame*>(QStringLiteral("cycleResultRow-%1-TC-107").arg(cycle));
        QVERIFY(broken);
        QStringList texts;
        for (auto* l : broken->findChildren<QLabel*>()) texts << l->text();
        QVERIFY2(texts.contains(label(Verdict::Fallido).toUpper()), qPrintable(texts.join(QStringLiteral(" | "))));

        // Y el caso fallado lleva a su ejecución completa, con sus pasos y sus evidencias.
        auto* open = f.window->findChild<QPushButton*>(QStringLiteral("cycleResultOpen-%1-TC-107").arg(cycle));
        QVERIFY(open);
        QTest::mouseClick(open, Qt::LeftButton);
        QCOMPARE(f.window->currentScreen(), Screen::Historial);

        // Lo desplegado se recuerda al volver: refrescar la pantalla no vuelve a plegarlo.
        f.window->navigate(Screen::Plan);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(f.window->findChild<QFrame*>(QStringLiteral("cycleResultRow-%1-TC-107").arg(cycle)));
    }

    // Un bug se encuentra ejecutando: cuelga de esa ejecución y se ve en sus resultados (los del
    // ciclo y los de la ejecución), no en la ficha del caso.
    void bugsBelongToTheRunTheyWereFoundInAndShowUpInItsResults() {
        WindowFixture f;
        const QString planId = f.app.plans.activeId();
        f.app.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Regresión"), planId);
        const QString cycle = f.app.run.planRunId();
        const QString runId = f.app.run.state().runId;
        IssueLink link;
        link.key = QStringLiteral("SHOP-77");
        link.caseId = QStringLiteral("TC-103");
        link.runId = runId;
        link.planRunId = cycle;
        link.step = 1;
        link.title = QStringLiteral("El correo no llega");
        link.severity = QStringLiteral("Mayor");
        link.url = QStringLiteral("https://acme.atlassian.net/browse/SHOP-77");
        link.createdAt = QDateTime::currentDateTime();
        f.app.bugLedger.recordIssue(link);
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Fail);
        f.window->finishRun();

        // En los resultados del ciclo, junto al caso del que salió.
        f.window->navigate(Screen::Plan);
        QTest::qWait(50);
        QTest::mouseClick(f.window->findChild<QPushButton*>(QStringLiteral("cycleHistoryToggle")), Qt::LeftButton);
        QTest::mouseClick(f.window->findChild<QPushButton*>(QStringLiteral("cycleResults-%1").arg(cycle)), Qt::LeftButton);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* chip = f.window->findChild<QPushButton*>(QStringLiteral("cycleResultBug-%1-SHOP-77").arg(cycle));
        QVERIFY2(chip, "el resultado del caso enseña el bug que salió de él");

        // Y en el detalle de la ejecución, que es donde se encontró. Da igual lo que el historial
        // estuviera enseñando (aquí, las métricas): lo que se pide es ver esa ejecución.
        f.window->showMetrics();
        f.window->navigate(Screen::Plan);
        QTest::mouseClick(f.window->findChild<QPushButton*>(QStringLiteral("cycleResultOpen-%1-TC-103").arg(cycle)), Qt::LeftButton);
        QTest::qWait(50);
        QVERIFY(f.window->findChild<QWidget*>(QStringLiteral("runBugs")));
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("openBug-SHOP-77")));

        // Un bug de otra ejecución del mismo caso no sale en ésta.
        IssueLink other = link;
        other.key = QStringLiteral("SHOP-78");
        other.runId = QStringLiteral("R-0099");
        other.planRunId = QStringLiteral("PR-0099");
        f.app.bugLedger.recordIssue(other);
        f.window->navigate(Screen::Plan);
        f.window->navigate(Screen::Historial);
        QTest::qWait(50);
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("openBug-SHOP-77")));
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("openBug-SHOP-78")));

        // La ficha del caso ya no habla de bugs: se ven donde se encontraron.
        f.window->navigate(Screen::Casos);
        QTest::qWait(50);
        auto* cases = f.window->findChild<CasesView*>();
        QVERIFY(cases);
        for (auto* l : cases->findChildren<QLabel*>())
            QVERIFY2(!l->text().contains(QStringLiteral("BUG")), qPrintable(l->text()));
    }

    void thePlanReportOffersDeletingEveryCycleButTheOneInProgress() {
        WindowFixture f;
        f.window->navigate(Screen::Historial);
        auto* history = f.window->findChild<HistoryView*>();
        QVERIFY(history);

        // Un ciclo terminado se puede eliminar.
        f.app.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Terminado"));
        const QString finished = f.app.run.planRunId();
        f.app.run.mark(StepResult::Pass);
        f.window->finishRun();
        history->showPlan(finished);
        QTest::qWait(50);
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("deletePlan")));

        // El que se está ejecutando, no: la ejecución sigue escribiendo en él.
        f.app.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("En curso"));
        const QString running = f.app.run.planRunId();
        history->showPlan(running);
        QTest::qWait(50);
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("deletePlan")));

        // Al borrar el terminado, el historial deja de listarlo y el informe abierto pasa a otro.
        QVERIFY(f.app.history.removePlanRun(finished));
        QTest::qWait(50);
        QVERIFY(!f.app.history.findPlan(finished));
        QVERIFY(f.app.history.findPlan(running));
    }

    // La conexión con GESREQ es general y su contraseña va al llavero; el sistema de GESREQ es del proyecto,
    // se elige en su configuración y no puede ser el de otro proyecto.
    void eachAiProviderKeepsItsOwnKeyAndModelInSettings() {
        WindowFixture f;
        f.window->openSettings();
        QWidget* dialog = f.window->settingsWindow();
        QVERIFY(dialog);
        auto* provider = dialog->findChild<QComboBox*>(QStringLiteral("settingsAiProvider"));
        auto* key = dialog->findChild<QLineEdit*>(QStringLiteral("settingsAiKey"));
        auto* model = dialog->findChild<QLineEdit*>(QStringLiteral("settingsAiModel"));
        QVERIFY(provider && key && model);
        QCOMPARE(model->placeholderText(), AiSettings::defaultModel(AiProvider::Anthropic));

        QTest::keyClicks(key, "sk-ant-123");
        QCOMPARE(f.app.secrets->values.value(QStringLiteral("ai/anthropic/apiKey")), QStringLiteral("sk-ant-123"));
        provider->setCurrentIndex(provider->findData(static_cast<int>(AiProvider::Gemini)));
        QCOMPARE(toString(f.app.settings.ai().provider), toString(AiProvider::Gemini));
        QVERIFY(key->text().isEmpty());   // la clave de Gemini, todavía vacía
        QCOMPARE(model->placeholderText(), AiSettings::defaultModel(AiProvider::Gemini));
        QTest::keyClicks(key, "AIza-9");
        QTest::keyClicks(model, "gemini-x");
        provider->setCurrentIndex(provider->findData(static_cast<int>(AiProvider::Anthropic)));
        QCOMPARE(key->text(), QStringLiteral("sk-ant-123"));
        QVERIFY(model->text().isEmpty());
        QCOMPARE(f.app.settings.ai().of(AiProvider::Gemini).model, QStringLiteral("gemini-x"));
        QCOMPARE(f.app.secrets->values.value(QStringLiteral("ai/gemini/apiKey")), QStringLiteral("AIza-9"));
    }

    void gesreqConnectionIsGeneralAndItsSystemBelongsToTheProject() {
        WindowFixture f;
        ProjectStore projects(std::make_shared<testing::MemoryProjectRepository>());
        QVERIFY(projects.load());
        const QString segran = projects.create(QStringLiteral("Riesgos"));
        QVERIFY(projects.setRequirementSystem(segran, QStringLiteral("SEGRAN")));
        f.ctx.projects = &projects;
        f.ctx.projectId = projects.activeId();
        f.window = std::make_unique<MainWindow>(f.ctx);
        f.window->show();
        f.window->openSettings();
        QWidget* dialog = f.window->settingsWindow();
        QVERIFY(dialog);
        auto* project = dialog->findChild<QWidget*>(QStringLiteral("projectSettingsSection"));
        auto* gesreq = dialog->findChild<QWidget*>(QStringLiteral("gesreqSettings"));
        auto* url = dialog->findChild<QLineEdit*>(QStringLiteral("settingsGesreqUrl"));
        auto* user = dialog->findChild<QLineEdit*>(QStringLiteral("settingsGesreqUser"));
        auto* password = dialog->findChild<QLineEdit*>(QStringLiteral("settingsGesreqPassword"));
        auto* system = dialog->findChild<QLineEdit*>(QStringLiteral("settingsGesreqSystem"));
        auto* note = dialog->findChild<QLabel*>(QStringLiteral("settingsGesreqSystemNote"));
        auto* badge = dialog->findChild<QPushButton*>(QStringLiteral("settingsGesreqBadge"));
        QVERIFY(project && gesreq && url && user && password && system && note && badge);
        QVERIFY(gesreq->isAncestorOf(url) && gesreq->isAncestorOf(password));
        QVERIFY(project->isAncestorOf(system));
        QVERIFY(!gesreq->isAncestorOf(system));

        QTest::keyClicks(url, "http://gesreq.test:7401/greq");
        QTest::keyClicks(user, "QAUSR0101");
        QTest::keyClicks(password, "s3creta");
        QCOMPARE(f.app.settings.requirementSource().url, QStringLiteral("http://gesreq.test:7401/greq"));
        QCOMPARE(f.app.settings.requirementSource().user, QStringLiteral("QAUSR0101"));
        QCOMPARE(f.app.secrets->values.value(QStringLiteral("gesreq/password")), QStringLiteral("s3creta"));
        QVERIFY(f.app.settingsRepo->requirementSource.password.isEmpty());

        // Probar la conexión la marca y trae los sistemas de la bandeja, que el aviso enseña.
        auto requirementOf = [](const QString& id, const QString& code) { ExternalRequirement r; r.id = id; r.systemCode = code; return r; };
        f.app.requirementSource->inbox = {requirementOf(QStringLiteral("2026310"), QStringLiteral("SUMA2SALIDA")),
                                          requirementOf(QStringLiteral("2025723"), QStringLiteral("SUMAOCE"))};
        badge->click();
        QVERIFY(f.app.settings.requirementSource().connected);
        QCOMPARE(f.app.requirementSource->tested.last().password, QStringLiteral("s3creta"));
        QVERIFY2(note->text().contains(QStringLiteral("SUMAOCE")), qPrintable(note->text()));

        // Un sistema que ya es de otro proyecto no se guarda, y el aviso dice de cuál.
        system->setFocus();
        QTest::keyClicks(system, "segran");
        QTest::keyClick(system, Qt::Key_Return);
        QVERIFY(projects.find(projects.activeId())->requirementSystem.isEmpty());
        QVERIFY2(note->text().contains(QStringLiteral("Riesgos")), qPrintable(note->text()));

        system->clear();
        QTest::keyClicks(system, "SUMA TRANSITO");
        QTest::keyClick(system, Qt::Key_Return);
        QCOMPARE(projects.find(projects.activeId())->requirementSystem, QStringLiteral("SUMA TRANSITO"));
        QCOMPARE(projects.projectForRequirementSystem(QStringLiteral("suma transito")), projects.activeId());
        QVERIFY(projects.find(segran)->requirementSystem == QStringLiteral("SEGRAN"));

        // Con otro gestor desaparece el código Jira, pero la configuración del proyecto sigue por GESREQ.
        // (Intro en el campo ha cerrado el diálogo, así que se mira si se verían con él abierto.)
        f.app.settings.updateTracker([](TrackerSettings& t) { t.kind = TrackerKind::GitHub; });
        QVERIFY(!project->isHidden());
        QVERIFY(!dialog->findChild<QLineEdit*>(QStringLiteral("settingsJiraProject"))->isVisibleTo(dialog));
        QVERIFY(system->isVisibleTo(dialog));
        f.window.reset();   // antes que el catálogo, al que la ventana sigue conectada
    }

    // El código Jira y el sistema de GESREQ del proyecto se eligen de lo que hay en cada sistema, buscando.
    void projectCodesArePickedFromWhatEachSystemHas() {
        WindowFixture f;
        ProjectStore projects(std::make_shared<testing::MemoryProjectRepository>());
        QVERIFY(projects.load());
        const QString risks = projects.create(QStringLiteral("Riesgos"));
        QVERIFY(projects.setRequirementSystem(risks, QStringLiteral("SEGRAN")));
        f.ctx.projects = &projects;
        f.ctx.projectId = projects.activeId();
        f.window = std::make_unique<MainWindow>(f.ctx);
        f.window->show();
        f.app.tracker->projectsToReturn = {TrackerProject{QStringLiteral("ADM"), QStringLiteral("Administración")},
                                           TrackerProject{QStringLiteral("SHOP"), QStringLiteral("Tienda online")}};
        f.app.requirementSource->catalog = {RequirementSystem{QStringLiteral("SEGRAN"), QStringLiteral("GESTIÓN DE RIESGOS")},
                                            RequirementSystem{QStringLiteral("SUMA TRANSITO"), QStringLiteral("TRANSITOS")},
                                            RequirementSystem{QStringLiteral("SUMAOCE"), QStringLiteral("OPERADORES DE COMERCIO EXTERIOR")}};
        ExternalRequirement assigned;
        assigned.id = QStringLiteral("2025723");
        assigned.systemCode = QStringLiteral("SUMAOCE");
        f.app.requirementSource->inbox = {assigned};
        f.app.requirements.testConnection([](const ConnectionResult&) {});   // deja leídos los sistemas de la bandeja

        f.window->openSettings();
        QWidget* dialog = f.window->settingsWindow();
        QVERIFY(dialog);
        auto* jiraPick = dialog->findChild<QPushButton*>(QStringLiteral("settingsJiraProjectPick"));
        auto* systemPick = dialog->findChild<QPushButton*>(QStringLiteral("settingsGesreqSystemPick"));
        QVERIFY(jiraPick && systemPick);
        QVERIFY(jiraPick->isEnabled());

        // Jira: se consulta al abrir, se busca por nombre y lo elegido pasa al proyecto.
        jiraPick->click();
        auto* jiraChoice = dialog->findChild<ChoiceDialog*>();
        QVERIFY(jiraChoice);
        QCOMPARE(f.app.tracker->projectListCalls, 1);
        QTest::keyClicks(jiraChoice->findChild<QLineEdit*>(QStringLiteral("choiceSearch")), "tienda");
        QCOMPARE(jiraChoice->findChild<QListWidget*>(QStringLiteral("choiceList"))->count(), 1);
        jiraChoice->findChild<QPushButton*>(QStringLiteral("choiceAccept"))->click();
        QCOMPARE(f.app.settings.tracker().project, QStringLiteral("SHOP"));
        QCOMPARE(dialog->findChild<QLineEdit*>(QStringLiteral("settingsJiraProject"))->text(), QStringLiteral("SHOP"));
        QTRY_VERIFY(!dialog->findChild<ChoiceDialog*>());   // se cierra y se libera

        // GESREQ: el catálogo, con los sistemas de la bandeja primero y el que ya es de otro proyecto señalado.
        systemPick->click();
        auto* systemChoice = dialog->findChild<ChoiceDialog*>();
        QVERIFY(systemChoice);
        auto* list = systemChoice->findChild<QListWidget*>(QStringLiteral("choiceList"));
        QCOMPARE(list->count(), 3);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toString(), QStringLiteral("SUMAOCE"));
        QVERIFY2(list->item(0)->text().contains(QStringLiteral("en tu bandeja")), qPrintable(list->item(0)->text()));
        QString segran;
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->data(Qt::UserRole).toString() == QStringLiteral("SEGRAN")) segran = list->item(i)->text();
        QVERIFY2(segran.contains(QStringLiteral("Riesgos")), qPrintable(segran));
        QTest::keyClicks(systemChoice->findChild<QLineEdit*>(QStringLiteral("choiceSearch")), "transitos");
        systemChoice->findChild<QPushButton*>(QStringLiteral("choiceAccept"))->click();
        QCOMPARE(projects.find(projects.activeId())->requirementSystem, QStringLiteral("SUMA TRANSITO"));
        QCOMPARE(dialog->findChild<QLineEdit*>(QStringLiteral("settingsGesreqSystem"))->text(), QStringLiteral("SUMA TRANSITO"));
        f.window.reset();   // antes que el catálogo, al que la ventana sigue conectada
    }

    // Issues: se importan de la bandeja los requerimientos del sistema del proyecto, se les vinculan casos y
    // planes, enseñan los resultados de sus pruebas y avisan de lo que cambia en GESREQ sin pisar lo escrito.
    void issuesOrganizeTheTestsOfTheRequirementsOfTheProject() {
        WindowFixture f;
        ProjectStore projects(std::make_shared<testing::MemoryProjectRepository>());
        QVERIFY(projects.load());
        QVERIFY(projects.setRequirementSystem(projects.activeId(), QStringLiteral("SUMA TRANSITO")));
        f.ctx.projects = &projects;
        f.ctx.projectId = projects.activeId();
        f.window = std::make_unique<MainWindow>(f.ctx);
        f.window->show();
        f.app.settings.updateRequirementSource([](RequirementSourceSettings& r) { r.url = QStringLiteral("http://gesreq.test:7401/greq"); });
        auto requirementOf = [](const QString& id, const QString& code, const QString& summary) {
            ExternalRequirement r;
            r.id = id;
            r.systemCode = code;
            r.system = code + QStringLiteral("-SISTEMA");
            r.summary = summary;
            r.priority = QStringLiteral("ALTA");
            r.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
            return r;
        };
        ExternalRequirement mine = requirementOf(QStringLiteral("2025175"), QStringLiteral("SUMA TRANSITO"), QStringLiteral("Desarrollo complementario del laboratorio"));
        f.app.requirementSource->inbox = {mine, requirementOf(QStringLiteral("2025719"), QStringLiteral("SEGRAN"), QStringLiteral("Módulo de riesgos"))};

        QVERIFY(f.nav(Screen::Issues));
        QVERIFY(f.nav(Screen::Issues)->toolTip().contains(QStringLiteral("Ctrl+6")));
        QTest::mouseClick(f.nav(Screen::Issues), Qt::LeftButton);
        QCOMPARE(f.window->currentScreen(), Screen::Issues);

        // La pestaña GREQS enseña toda la bandeja, con lo que ya tiene issue y lo que no; leerla no crea
        // nada, y las pruebas se empiezan desde la fila del requerimiento.
        f.openGreqs();
        QCOMPARE(f.app.requirementSource->inboxReads, 1);
        QVERIFY(f.greqAction(QStringLiteral("2025175")) && f.greqAction(QStringLiteral("2025719")));
        QVERIFY2(f.greqStatus(QStringLiteral("2025175")).contains(QStringLiteral("SIN ISSUE")), qPrintable(f.greqStatus(QStringLiteral("2025175"))));
        const QString count = f.liveLabel("greqsCount")->text();
        QVERIFY2(count.contains(QStringLiteral("2 asignados")) && count.contains(QStringLiteral("2 sin issue")), qPrintable(count));
        QVERIFY(f.app.issues.issues().isEmpty());
        f.greqAction(QStringLiteral("2025175"))->click();
        QCOMPARE(f.app.issues.issues().size(), 1);
        const QString id = f.app.issues.issues().first().id;
        QCOMPARE(f.app.issues.selectedId(), id);
        auto* title = f.window->findChild<QLineEdit*>(QStringLiteral("issueTitle"));
        QCOMPARE(title->text(), QStringLiteral("Desarrollo complementario del laboratorio"));
        // El issue importado nace ya en el gestor: sus casos, bugs y resultados tienen dónde colgarse.
        QCOMPARE(f.app.tracker->publishedIssues.size(), 1);
        QVERIFY(f.app.issues.find(id)->isPublished());
        QVERIFY(f.app.tracker->publishedIssues.first().description.contains(QStringLiteral("2025175")));

        // Y con su plan de pruebas listo, que es con lo que se prueba el requerimiento.
        QCOMPARE(f.app.issues.find(id)->planIds.size(), 1);
        const QString planId = f.app.issues.find(id)->planIds.first();
        QCOMPARE(f.app.plans.activeId(), planId);
        QVERIFY(f.app.plans.find(planId)->name.contains(id));
        f.app.plans.toggle(QStringLiteral("TC-104"));

        // El plan de pruebas dice a qué issue prueba, y la etiqueta lleva hasta él: es donde está el
        // porqué de sus casos.
        f.window->navigate(Screen::Plan);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* issueTag = f.window->findChild<QPushButton*>(QStringLiteral("planIssueTag-%1").arg(id));
        QVERIFY2(issueTag, "el plan del issue enseña su etiqueta");
        QVERIFY(issueTag->text().contains(id));
        issueTag->click();
        QCOMPARE(f.window->currentScreen(), Screen::Issues);
        QCOMPARE(f.app.issues.selectedId(), id);

        f.window->navigate(Screen::Issues);
        // El paso 1 de la revisión enseña el plan del issue con sus casos dentro: no hay tarjeta aparte.
        const QString planStep = f.liveLabel("issueStepPlanDetail")->text();
        QVERIFY2(planStep.contains(id) && planStep.contains(QStringLiteral("1 caso")), qPrintable(planStep));
        QVERIFY(!f.window->findChild<QLabel*>(QStringLiteral("issuePlansHeader")));
        QVERIFY(!f.window->findChild<QLabel*>(QStringLiteral("issueCasesHeader")));

        // Una ejecución suelta de ese caso no es un resultado del issue; un ciclo de su plan, sí.
        f.app.run.start(QStringLiteral("TC-104"));
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);
        f.app.run.finish();
        f.window->navigate(Screen::Issues);
        QVERIFY(IssueStore::runsOf(*f.app.issues.find(id), f.app.history).isEmpty());
        QVERIFY2(f.liveLabel("issueStepRunDetail")->text().contains(QStringLiteral("0 ejecución")),
                 "sin ciclos del plan no hay resultados del issue");

        f.app.run.startSequence({QStringLiteral("TC-104")}, QStringLiteral("Plan del issue"), planId);
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);
        f.app.run.finish();
        f.window->navigate(Screen::Issues);
        QCOMPARE(IssueStore::runsOf(*f.app.issues.find(id), f.app.history).size(), 1);
        const QString runStep = f.liveLabel("issueStepRunDetail")->text();
        QVERIFY2(runStep.contains(QStringLiteral("1 ejecución")), qPrintable(runStep));

        // Lo escrito en QAflow se queda aunque GESREQ cambie; el cambio se avisa hasta revisarlo.
        title->selectAll();
        QTest::keyClicks(title, "Mi titulo");
        QTest::keyClick(title, Qt::Key_Return);
        QCOMPARE(f.app.issues.find(id)->title, QStringLiteral("Mi titulo"));
        // Volver a leer la bandeja pone al día lo importado; la fila ya dice cuál es su issue y lo abre.
        mine.states = {QStringLiteral("CONTROL DE CALIDAD OBSERVADO")};
        f.app.requirementSource->inbox = {mine};
        f.openGreqs();
        f.liveButton(QStringLiteral("greqsReload"))->click();
        QCOMPARE(f.greqStatus(QStringLiteral("2025175")), id);
        f.greqAction(QStringLiteral("2025175"))->click();
        QCOMPARE(f.app.issues.issues().size(), 1);
        QCOMPARE(f.app.issues.find(id)->title, QStringLiteral("Mi titulo"));
        QCOMPARE(f.app.tracker->publishedIssues.size(), 1);   // volver a empezar no crea otro issue
        auto* changes = f.window->findChild<QWidget*>(QStringLiteral("issueChanges"));
        QVERIFY(changes && !changes->isHidden());
        QVERIFY(f.badge(Screen::Issues)->isVisible());
        f.window->findChild<QPushButton*>(QStringLiteral("issueAcknowledge"))->click();
        QVERIFY(changes->isHidden());
        QVERIFY(!f.badge(Screen::Issues)->isVisible());
        f.window.reset();   // antes que el catálogo, al que la ventana sigue conectada
    }

    // Sin la conexión con GESREQ configurada, la pestaña no lee nada y lleva a los ajustes.
    void theGreqsTabWithoutAConnectionOpensTheSettings() {
        WindowFixture f;
        f.openGreqs();
        QCOMPARE(f.app.requirementSource->inboxReads, 0);
        f.liveButton(QStringLiteral("greqsSettings"))->click();
        QVERIFY(f.window->settingsWindow());
    }

    // La bandeja es del usuario: también enseña los requerimientos de otros sistemas. Iniciar sus pruebas
    // pide activar el proyecto que los trabaja (la ventana no cambia de proyecto por su cuenta); el del
    // sistema propio se abre aquí mismo, y el de un sistema sin proyecto no se puede empezar.
    void startingTestsOfAnotherSystemAsksForItsProject() {
        WindowFixture f;
        ProjectStore projects(std::make_shared<testing::MemoryProjectRepository>());
        QVERIFY(projects.load());
        const QString mineId = projects.activeId();
        const QString otherId = projects.create(QStringLiteral("Riesgos"));
        QVERIFY(projects.setRequirementSystem(mineId, QStringLiteral("SUMA TRANSITO")));
        QVERIFY(projects.setRequirementSystem(otherId, QStringLiteral("SEGRAN")));
        f.ctx.projects = &projects;
        f.ctx.projectId = mineId;
        f.window = std::make_unique<MainWindow>(f.ctx);
        f.window->show();
        f.app.settings.updateRequirementSource([](RequirementSourceSettings& r) { r.url = QStringLiteral("http://gesreq.test:7401/greq"); });
        auto requirementOf = [](const QString& id, const QString& code, const QString& summary) {
            ExternalRequirement r;
            r.id = id;
            r.systemCode = code;
            r.system = code + QStringLiteral("-SISTEMA");
            r.summary = summary;
            r.priority = QStringLiteral("ALTA");
            r.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
            return r;
        };
        f.app.requirementSource->inbox = {requirementOf(QStringLiteral("2025719"), QStringLiteral("SEGRAN"), QStringLiteral("Módulo de riesgos")),
                                          requirementOf(QStringLiteral("2025175"), QStringLiteral("SUMA TRANSITO"), QStringLiteral("Cupones de descuento")),
                                          requirementOf(QStringLiteral("2026001"), QStringLiteral("SIN PROYECTO"), QStringLiteral("Algo de otro sistema"))};

        QSignalSpy started(f.window.get(), &MainWindow::startTestingRequested);
        f.openGreqs();
        // Cada fila dice en qué proyecto se prueba su requerimiento, y su botón, dónde se empieza.
        QCOMPARE(f.greqAction(QStringLiteral("2025175"))->text(), QStringLiteral("Iniciar pruebas"));
        QVERIFY2(f.greqAction(QStringLiteral("2025719"))->text().contains(QStringLiteral("Riesgos")),
                 qPrintable(f.greqAction(QStringLiteral("2025719"))->text()));
        // El de un sistema que nadie trabaja dice que no tiene proyecto, pero se puede empezar igual:
        // primero se elige o se crea (ver startingTestsOfASystemNobodyWorksAsksForItsProject).
        QVERIFY2(f.greqAction(QStringLiteral("2026001"))->text().contains(QStringLiteral("Elegir proyecto")),
                 qPrintable(f.greqAction(QStringLiteral("2026001"))->text()));

        f.greqAction(QStringLiteral("2025719"))->click();
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.first().at(0).toString(), otherId);
        QCOMPARE(started.first().at(1).value<ExternalRequirement>().id, QStringLiteral("2025719"));
        QVERIFY(f.app.issues.issues().isEmpty());   // el issue es del otro proyecto, no de éste

        // El del sistema del proyecto se abre aquí, sin pedir cambio de proyecto.
        f.greqAction(QStringLiteral("2025175"))->click();
        QCOMPARE(started.count(), 1);
        QCOMPARE(f.app.issues.issues().size(), 1);
        const Issue& issue = f.app.issues.issues().first();
        QCOMPARE(issue.requirement.data.id, QStringLiteral("2025175"));
        QCOMPARE(f.app.issues.selectedId(), issue.id);
        QCOMPARE(f.window->currentScreen(), Screen::Issues);
        f.window.reset();   // antes que el catálogo, al que la ventana sigue conectada
    }

    // Empezar las pruebas de un requerimiento cuyo sistema no trabaja nadie: el diálogo de alta crea el
    // proyecto con sus dos códigos (el de Jira se pide aparte, porque es de los ajustes de ese proyecto) y
    // las pruebas siguen allí.
    void startingTestsOfASystemNobodyWorksAsksForItsProject() {
        WindowFixture f;
        ProjectStore projects(std::make_shared<testing::MemoryProjectRepository>());
        QVERIFY(projects.load());
        const QString mineId = projects.activeId();
        QVERIFY(projects.setRequirementSystem(mineId, QStringLiteral("SUMA TRANSITO")));
        f.ctx.projects = &projects;
        f.ctx.projectId = mineId;
        f.window = std::make_unique<MainWindow>(f.ctx);
        f.window->show();
        ExternalRequirement r;
        r.id = QStringLiteral("2026001");
        r.systemCode = QStringLiteral("SUMA2SALIDA");
        r.system = QStringLiteral("SUMA2SALIDA-SALIDAS");
        r.summary = QStringLiteral("Salidas de almacén");
        r.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
        f.app.requirementSource->inbox = {r};
        f.app.settings.updateRequirementSource([](RequirementSourceSettings& rs) { rs.url = QStringLiteral("http://gesreq.test:7401/greq"); });

        QSignalSpy started(f.window.get(), &MainWindow::startTestingRequested);
        QSignalSpy jiraKey(f.window.get(), &MainWindow::projectJiraKeyRequested);
        f.openGreqs();
        auto* start = f.greqAction(QStringLiteral("2026001"));
        QVERIFY(start);
        QVERIFY2(start->text().contains(QStringLiteral("Elegir proyecto")), qPrintable(start->text()));
        start->click();

        auto* setup = f.window->findChild<ProjectSetupDialog*>();
        QVERIFY(setup);
        // Llega con el sistema escrito y con un nombre de partida, que se puede cambiar.
        QCOMPARE(setup->findChild<QLineEdit*>(QStringLiteral("projectSetupSystem"))->text(), QStringLiteral("SUMA2SALIDA"));
        auto* name = setup->findChild<QLineEdit*>(QStringLiteral("projectSetupName"));
        QCOMPARE(name->text(), QStringLiteral("SUMA2SALIDA"));
        name->setText(QStringLiteral("Salidas"));
        setup->findChild<QLineEdit*>(QStringLiteral("projectSetupJira"))->setText(QStringLiteral("SAL"));
        setup->findChild<QPushButton*>(QStringLiteral("projectSetupAccept"))->click();

        QCOMPARE(projects.projects().size(), 2);
        const QString created = projects.projectForRequirementSystem(QStringLiteral("SUMA2SALIDA"));
        QVERIFY(!created.isEmpty());
        QCOMPARE(projects.find(created)->name, QStringLiteral("Salidas"));
        QCOMPARE(jiraKey.count(), 1);
        QCOMPARE(jiraKey.first().at(0).toString(), created);
        QCOMPARE(jiraKey.first().at(1).toString(), QStringLiteral("SAL"));
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.first().at(0).toString(), created);
        QCOMPARE(started.first().at(1).value<ExternalRequirement>().id, QStringLiteral("2026001"));
        QVERIFY(f.app.issues.issues().isEmpty());   // el issue es del proyecto nuevo, no de éste
        QTRY_VERIFY(!f.window->findChild<ProjectSetupDialog*>());
        f.window.reset();   // antes que el catálogo, al que la ventana sigue conectada
    }

    // El mismo diálogo vincula el sistema a un proyecto que ya existe; si es el abierto, las pruebas
    // empiezan aquí mismo sin pedir cambio de proyecto.
    void theProjectOfARequirementCanBeOneThatAlreadyExists() {
        WindowFixture f;
        ProjectStore projects(std::make_shared<testing::MemoryProjectRepository>());
        QVERIFY(projects.load());
        const QString mineId = projects.activeId();
        QVERIFY(projects.setRequirementSystem(mineId, QStringLiteral("SUMA TRANSITO")));
        f.ctx.projects = &projects;
        f.ctx.projectId = mineId;
        f.window = std::make_unique<MainWindow>(f.ctx);
        f.window->show();
        ExternalRequirement r;
        r.id = QStringLiteral("2026002");
        r.systemCode = QStringLiteral("SEGRAN");
        r.system = QStringLiteral("SEGRAN-RIESGOS");
        r.summary = QStringLiteral("Módulo de riesgos");
        r.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
        f.app.requirementSource->inbox = {r};
        f.app.settings.updateRequirementSource([](RequirementSourceSettings& rs) { rs.url = QStringLiteral("http://gesreq.test:7401/greq"); });

        QSignalSpy started(f.window.get(), &MainWindow::startTestingRequested);
        f.openGreqs();
        f.greqAction(QStringLiteral("2026002"))->click();

        auto* setup = f.window->findChild<ProjectSetupDialog*>();
        QVERIFY(setup);
        auto* target = setup->findChild<QComboBox*>(QStringLiteral("projectSetupTarget"));
        QVERIFY(target);
        const int mine = target->findData(mineId);
        QVERIFY(mine > 0);   // el primero es «Proyecto nuevo…»
        target->setCurrentIndex(mine);
        // Elegido un proyecto que ya existe, ni su nombre ni su código Jira se tocan aquí.
        QVERIFY(setup->findChild<QLineEdit*>(QStringLiteral("projectSetupName"))->parentWidget()->isHidden());
        QVERIFY2(setup->findChild<QLabel*>(QStringLiteral("projectSetupNote"))->text().contains(QStringLiteral("SUMA TRANSITO")),
                 qPrintable(setup->findChild<QLabel*>(QStringLiteral("projectSetupNote"))->text()));
        setup->findChild<QPushButton*>(QStringLiteral("projectSetupAccept"))->click();

        QCOMPARE(projects.projects().size(), 1);   // no se creó ninguno
        QCOMPARE(projects.find(mineId)->requirementSystem, QStringLiteral("SEGRAN"));
        QCOMPARE(started.count(), 0);              // es el proyecto abierto: se abre aquí mismo
        QCOMPARE(f.app.issues.issues().size(), 1);
        QCOMPARE(f.app.issues.issues().first().requirement.data.id, QStringLiteral("2026002"));
        f.window.reset();   // antes que el catálogo, al que la ventana sigue conectada
    }

    // El tablero es una vista general: también enseña los issues de los demás proyectos, atenuados y con su
    // proyecto, y los de éste resaltan. Seguir con uno ajeno es cambiar de proyecto, y se pregunta antes.
    void theBoardShowsTheIssuesOfEveryProjectAndAsksBeforeSwitching() {
        WindowFixture f;
        ProjectStore projects(std::make_shared<testing::MemoryProjectRepository>());
        QVERIFY(projects.load());
        const QString mineId = projects.activeId();
        const QString otherId = projects.create(QStringLiteral("Riesgos"));
        auto otherRepo = std::make_shared<testing::MemoryIssueRepository>();
        {
            IssueStore seed(otherRepo);
            seed.load();
            seed.createIssue(QStringLiteral("Módulo de riesgos"));   // IS-0001 allí…
        }
        const QString mine = f.app.issues.createIssue(QStringLiteral("Cupones de descuento"));   // …y aquí
        IssueDirectory directory(projects, [&](const QString& id) -> std::shared_ptr<IIssueRepository> {
            return id == otherId ? otherRepo : nullptr;
        });
        directory.attach(mineId, &f.app.issues);
        f.ctx.projects = &projects;
        f.ctx.projectId = mineId;
        f.ctx.issueDirectory = &directory;
        f.window = std::make_unique<MainWindow>(f.ctx);
        f.window->show();
        f.window->navigate(Screen::Issues);

        auto* own = f.liveButton(QStringLiteral("issueRow-%1").arg(mine));
        QVERIFY(own && own->property("own").toBool());
        const QString foreignName = QStringLiteral("issueForeignRow-%1-IS-0001").arg(otherId);
        auto* foreign = f.liveButton(foreignName);
        QVERIFY2(foreign, "el issue del otro proyecto está en el tablero");
        QVERIFY(foreign->property("foreign").toBool());
        QCOMPARE(f.window->findChild<QLabel*>(QStringLiteral("issueForeignProject-%1-IS-0001").arg(otherId))->text(), QStringLiteral("Riesgos"));

        // Un clic lo elige (y suelta el de este proyecto); su panel dice de dónde es y cómo seguir.
        foreign->click();
        QVERIFY(f.app.issues.selectedId().isEmpty());
        auto* go = f.liveButton(QStringLiteral("issueForeignOpen"));
        QVERIFY(go);
        QVERIFY2(go->text().contains(QStringLiteral("Riesgos")), qPrintable(go->text()));

        QSignalSpy wanted(f.window.get(), &MainWindow::openIssueInProjectRequested);
        go->click();
        auto* confirm = f.window->findChild<QMessageBox*>(QStringLiteral("issueSwitchConfirm"));
        QVERIFY(confirm);
        QCOMPARE(wanted.count(), 0);   // nada cambia de proyecto sin confirmarlo
        confirm->findChild<QPushButton*>(QStringLiteral("issueSwitchAccept"))->click();
        QCOMPARE(wanted.count(), 1);
        QCOMPARE(wanted.first().at(0).toString(), otherId);
        QCOMPARE(wanted.first().at(1).toString(), QStringLiteral("IS-0001"));

        // Ya en su proyecto, quien coordina el cambio abre allí el issue.
        f.window->openIssue(mine);
        QCOMPARE(f.window->currentScreen(), Screen::Issues);
        QCOMPARE(f.app.issues.selectedId(), mine);
        QCOMPARE(f.window->findChild<QLineEdit*>(QStringLiteral("issueTitle"))->text(), QStringLiteral("Cupones de descuento"));

        // Desde la lista, un doble clic en uno ajeno pregunta una sola vez —aunque llegue repetido— y la
        // confirmación se puede aceptar.
        f.window->navigate(Screen::Issues);
        auto* issuesView = f.window->findChild<IssuesView*>();
        issuesView->setListMode(true);
        auto* table = f.window->findChild<QTableWidget*>(QStringLiteral("issueListTable"));
        int foreignRow = -1;
        for (int r = 0; r < table->rowCount(); ++r)
            if (table->item(r, 3)->text() == QStringLiteral("Riesgos")) foreignRow = r;
        QVERIFY(foreignRow >= 0);
        emit table->cellDoubleClicked(foreignRow, 0);
        emit table->cellDoubleClicked(foreignRow, 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QCOMPARE(f.window->findChildren<QMessageBox*>(QStringLiteral("issueSwitchConfirm")).size(), 1);
        f.window->findChild<QMessageBox*>(QStringLiteral("issueSwitchConfirm"))->findChild<QPushButton*>(QStringLiteral("issueSwitchAccept"))->click();
        QCOMPARE(wanted.count(), 2);
        QCOMPARE(wanted.last().at(0).toString(), otherId);
        issuesView->setListMode(false);

        // «Sólo este proyecto» deja fuera a los demás.
        f.window->navigate(Screen::Issues);
        auto* scope = f.window->findChild<QComboBox*>(QStringLiteral("issueScopeFilter"));
        scope->setCurrentIndex(1);
        QVERIFY(!f.liveButton(foreignName));
        QVERIFY(!f.liveButton(QStringLiteral("issueRow-%1").arg(mine))->property("own").toBool());
        scope->setCurrentIndex(0);   // se recuerda: se deja como estaba
        QVERIFY(f.liveButton(foreignName));
        f.window.reset();   // antes que el catálogo, al que la ventana sigue conectada
    }

    // Cada página tiene sus pestañas, y todas marcan la página que se ve: cambiar de una a otra no deja
    // dos subrayadas.
    void theScreenTabsUnderlineThePageThatIsShown() {
        WindowFixture f;
        f.window->navigate(Screen::Issues);
        auto tab = [&](const char* name) { return f.window->findChild<QPushButton*>(QString::fromLatin1(name)); };
        QVERIFY(tab("issuesTabBoard")->isChecked() && !tab("issuesTabGreqs")->isChecked());
        QVERIFY(!tab("issuesTabHistory"));   // la lista no es una pestaña: es otra vista del tablero

        tab("issuesTabGreqs")->click();
        QVERIFY(tab("greqsTabGreqs")->isChecked() && !tab("greqsTabBoard")->isChecked());
        QVERIFY(tab("issuesTabGreqs")->isChecked() && !tab("issuesTabBoard")->isChecked());
        tab("greqsTabGreqs")->click();   // pulsar la elegida no la desmarca
        QVERIFY(tab("greqsTabGreqs")->isChecked());

        tab("greqsTabBoard")->click();
        QVERIFY(tab("issuesTabBoard")->isChecked() && !tab("issuesTabGreqs")->isChecked());
        QVERIFY(tab("greqsTabBoard")->isChecked() && !tab("greqsTabGreqs")->isChecked());
    }

    // Los finalizados hace tiempo salen del tablero; la vista de lista, al lado de la búsqueda, los tiene
    // todos, con los mismos filtros, y abre cualquiera.
    void finishedIssuesLeaveTheBoardAndStayInTheList() {
        WindowFixture f;
        const QString recent = f.app.issues.createIssue(QStringLiteral("Finalizado ayer"));
        const QString old = f.app.issues.createIssue(QStringLiteral("Finalizado hace un mes"));
        const QString open = f.app.issues.createIssue(QStringLiteral("En pruebas"));
        auto finish = [&](const QString& id, int daysAgo) {
            f.app.issues.updateIssue(id, [daysAgo](Issue& i) {
                IssueRevision r;
                r.startedAt = QDateTime::currentDateTime().addDays(-daysAgo - 2);
                r.closedAt = QDateTime::currentDateTime().addDays(-daysAgo);
                r.outcome = QaOutcome::Conforme;
                i.revisions = {r};
                i.state = IssueState::Done;
            });
        };
        finish(recent, 1);
        finish(old, 30);
        f.window->navigate(Screen::Issues);
        auto* view = f.window->findChild<IssuesView*>();
        view->setListMode(false);
        QVERIFY(f.liveButton(QStringLiteral("issueRow-%1").arg(recent)));
        QVERIFY(f.liveButton(QStringLiteral("issueRow-%1").arg(open)));
        QVERIFY2(!f.liveButton(QStringLiteral("issueRow-%1").arg(old)), "el finalizado hace un mes ya no está en el tablero");
        // Buscándolo, sí aparece: no se esconde lo que se busca.
        auto* search = f.window->findChild<QLineEdit*>(QStringLiteral("issueSearch"));
        search->setText(QStringLiteral("mes"));
        QVERIFY(f.liveButton(QStringLiteral("issueRow-%1").arg(old)));
        search->clear();

        // El pie de la columna lo dice y lleva a la lista, con los finalizados más recientes primero.
        auto* older = f.liveButton(QStringLiteral("issueBoardOlderDone"));
        QVERIFY(older);
        QVERIFY2(older->text().contains(QStringLiteral("1 finalizado")), qPrintable(older->text()));
        older->click();
        QVERIFY(view->isListMode());
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("issueViewList"))->isChecked());
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("issueViewBoard"))->isChecked());
        auto* table = f.window->findChild<QTableWidget*>(QStringLiteral("issueListTable"));
        QVERIFY(table && table->isVisible());
        QCOMPARE(table->rowCount(), 3);
        QCOMPARE(table->item(0, 0)->text(), recent);   // el finalizado más reciente primero
        QCOMPARE(table->item(1, 0)->text(), old);

        // Los filtros del tablero valen también para la lista.
        search->setText(QStringLiteral("mes"));
        QCOMPARE(table->rowCount(), 1);
        search->clear();

        // Un clic elige el issue y abre su panel; doble clic lo abre entero.
        int row = -1;
        for (int r = 0; r < table->rowCount(); ++r)
            if (table->item(r, 0)->text() == old) row = r;
        QVERIFY(row >= 0);
        table->setCurrentCell(row, 2);
        QCOMPARE(f.app.issues.selectedId(), old);
        QVERIFY(f.window->findChild<QWidget*>(QStringLiteral("issueDrawer"))->isVisible());
        emit table->cellDoubleClicked(row, 2);
        QCOMPARE(f.window->findChild<QLineEdit*>(QStringLiteral("issueTitle"))->text(), QStringLiteral("Finalizado hace un mes"));

        // El conmutador vuelve al tablero (y se recuerda: se deja como estaba).
        f.window->findChild<QPushButton*>(QStringLiteral("issuesTabBoard"))->click();
        f.window->findChild<QPushButton*>(QStringLiteral("issueViewBoard"))->click();
        QVERIFY(!view->isListMode());
        QVERIFY(!table->isVisible());
    }

    // GREQS dice qué requerimientos de la bandeja tienen issue (y en qué proyecto) y busca por su número
    // uno que no está asignado al usuario, para empezar sus pruebas igual.
    void theGreqsTabSaysWhichRequirementsHaveAnIssueAndFindsAnyByNumber() {
        WindowFixture f;
        ProjectStore projects(std::make_shared<testing::MemoryProjectRepository>());
        QVERIFY(projects.load());
        const QString mineId = projects.activeId();
        const QString otherId = projects.create(QStringLiteral("Riesgos"));
        QVERIFY(projects.setRequirementSystem(mineId, QStringLiteral("SUMA TRANSITO")));
        QVERIFY(projects.setRequirementSystem(otherId, QStringLiteral("SEGRAN")));
        const QString connection = QStringLiteral("http://gesreq.test:7401/greq");
        f.app.settings.updateRequirementSource([&](RequirementSourceSettings& r) { r.url = connection; });
        auto requirementOf = [](const QString& id, const QString& code) {
            ExternalRequirement r;
            r.id = id;
            r.systemCode = code;
            r.system = code + QStringLiteral("-SISTEMA");
            r.summary = QStringLiteral("Requerimiento %1").arg(id);
            r.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
            return r;
        };
        // 2025719 ya tiene issue en «Riesgos»; 2025175 todavía no tiene ninguno.
        auto otherRepo = std::make_shared<testing::MemoryIssueRepository>();
        {
            IssueStore seed(otherRepo);
            seed.load();
            seed.openForRequirement(requirementOf(QStringLiteral("2025719"), QStringLiteral("SEGRAN")), connection);
        }
        f.app.requirementSource->inbox = {requirementOf(QStringLiteral("2025175"), QStringLiteral("SUMA TRANSITO")),
                                          requirementOf(QStringLiteral("2025719"), QStringLiteral("SEGRAN"))};
        RequirementDetail detail;
        detail.id = QStringLiteral("2025800");
        detail.systemCode = QStringLiteral("SUMA TRANSITO");
        detail.description = QStringLiteral("Cupones de descuento\nCon su vigencia.");
        detail.state = QStringLiteral("CONTROL FUNCIONAL");
        f.app.requirementSource->details.insert(detail.id, detail);
        IssueDirectory directory(projects, [&](const QString& id) -> std::shared_ptr<IIssueRepository> {
            return id == otherId ? otherRepo : nullptr;
        });
        directory.attach(mineId, &f.app.issues);
        f.ctx.projects = &projects;
        f.ctx.projectId = mineId;
        f.ctx.issueDirectory = &directory;
        f.window = std::make_unique<MainWindow>(f.ctx);
        f.window->show();

        f.openGreqs();
        QVERIFY2(f.greqStatus(QStringLiteral("2025719")).contains(QStringLiteral("Riesgos")), qPrintable(f.greqStatus(QStringLiteral("2025719"))));
        QVERIFY(f.greqStatus(QStringLiteral("2025175")).contains(QStringLiteral("SIN ISSUE")));
        // Seguir con el issue de otro proyecto también se pregunta antes.
        QSignalSpy wanted(f.window.get(), &MainWindow::openIssueInProjectRequested);
        f.greqAction(QStringLiteral("2025719"))->click();
        auto* confirm = f.window->findChild<QMessageBox*>(QStringLiteral("issueSwitchConfirm"));
        QVERIFY(confirm);
        confirm->findChild<QPushButton*>(QStringLiteral("issueSwitchAccept"))->click();
        QCOMPARE(wanted.count(), 1);
        QCOMPARE(wanted.first().at(0).toString(), otherId);

        // El filtro «Sin issue» deja sólo lo que falta empezar.
        auto* scope = f.window->findChild<QComboBox*>(QStringLiteral("greqsIssueFilter"));
        scope->setCurrentIndex(scope->findData(1));
        QVERIFY(!f.greqAction(QStringLiteral("2025719")));
        QVERIFY(f.greqAction(QStringLiteral("2025175")));
        scope->setCurrentIndex(0);

        // Uno que no es del usuario se busca por su número: se lee su ficha y se empieza como cualquier otro.
        auto* number = f.window->findChild<QLineEdit*>(QStringLiteral("greqsNumber"));
        number->setText(QStringLiteral("9999999"));
        f.liveButton(QStringLiteral("greqsFind"))->click();
        QVERIFY2(f.liveLabel("greqsNotice")->text().contains(QStringLiteral("9999999")), qPrintable(f.liveLabel("greqsNotice")->text()));
        number->setText(QStringLiteral("2025800"));
        f.liveButton(QStringLiteral("greqsFind"))->click();
        auto* row = f.window->findChild<QFrame*>(QStringLiteral("greqRow-2025800"));
        QVERIFY(row);
        bool notAssigned = false;
        for (auto* l : row->findChildren<QLabel*>()) notAssigned |= l->text().contains(QStringLiteral("NO ASIGNADO"));
        QVERIFY(notAssigned);
        f.greqAction(QStringLiteral("2025800"))->click();
        const Issue* created = f.app.issues.findByRequirement(connection, QStringLiteral("2025800"));
        QVERIFY(created);
        QCOMPARE(created->title, QStringLiteral("Cupones de descuento"));
        QCOMPARE(f.app.issues.selectedId(), created->id);
        f.window.reset();   // antes que el catálogo, al que la ventana sigue conectada
    }

    // «Nuevo proyecto…» de la barra: el nombre es lo único obligatorio; los dos códigos son opcionales.
    void theNewProjectDialogTakesBothCodesAndTheyAreOptional() {
        WindowFixture f;
        ProjectStore projects(std::make_shared<testing::MemoryProjectRepository>());
        QVERIFY(projects.load());
        f.ctx.projects = &projects;
        f.ctx.projectId = projects.activeId();
        f.window = std::make_unique<MainWindow>(f.ctx);
        f.window->show();
        QSignalSpy switched(f.window.get(), &MainWindow::projectSwitchRequested);
        f.window->findChild<QPushButton*>(QStringLiteral("projectMenu"))->menu()->actions().at(0)->trigger();

        auto* setup = f.window->findChild<ProjectSetupDialog*>();
        QVERIFY(setup);
        QVERIFY(!setup->findChild<QComboBox*>(QStringLiteral("projectSetupTarget")));   // aquí sólo se crea
        auto* accept = setup->findChild<QPushButton*>(QStringLiteral("projectSetupAccept"));
        accept->click();   // sin nombre no se crea nada y el diálogo sigue abierto
        QCOMPARE(projects.projects().size(), 1);
        QVERIFY(f.window->findChild<ProjectSetupDialog*>());
        setup->findChild<QLineEdit*>(QStringLiteral("projectSetupName"))->setText(QStringLiteral("Riesgos"));
        accept->click();

        QCOMPARE(projects.projects().size(), 2);
        QCOMPARE(switched.count(), 1);
        const QString created = switched.first().at(0).toString();
        QCOMPARE(projects.find(created)->name, QStringLiteral("Riesgos"));
        QVERIFY(projects.find(created)->requirementSystem.isEmpty());   // sin sistema: se vincula cuando toque
        f.window.reset();   // antes que el catálogo, al que la ventana sigue conectada
    }

    // El issue se publica en el gestor revisando antes lo que se envía, y luego avisa de lo que cambió en
    // QAflow y sigue sin actualizar allí.
    void anIssueIsPublishedInTheTrackerAndSaysWhenItIsPendingToUpdate() {
        WindowFixture f;
        const QString id = f.app.issues.createIssue(QStringLiteral("Pruebas del laboratorio"));
        f.window->navigate(Screen::Issues);
        // La publicación en el gestor no tiene tarjeta: se hace desde el menú del tag de la cabecera.
        QVERIFY(!f.window->findChild<QWidget*>(QStringLiteral("issueJiraCard")));
        auto* publish = f.action("issuePublish");
        QVERIFY(publish);
        QVERIFY(publish->isVisible() && publish->isEnabled());
        QVERIFY(f.window->findChild<QWidget*>(QStringLiteral("issueJiraPending"))->isHidden());

        publish->trigger();
        auto* dialog = f.window->findChild<JiraPublishDialog*>();
        QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QLineEdit*>(QStringLiteral("jiraSummary"))->text(), QStringLiteral("Pruebas del laboratorio"));
        QVERIFY(dialog->findChild<QComboBox*>(QStringLiteral("jiraIssueType")) != nullptr);
        dialog->findChild<QPushButton*>(QStringLiteral("jiraPublishAccept"))->click();
        QTRY_VERIFY(!f.window->findChild<JiraPublishDialog*>());

        QVERIFY(f.app.issues.find(id)->isPublished());
        const QString key = f.app.issues.find(id)->publication.key;
        QCOMPARE(f.app.tracker->publishedIssues.size(), 1);
        QCOMPARE(f.app.tracker->publishedIssues.first().summary, QStringLiteral("Pruebas del laboratorio"));
        QVERIFY(f.app.tracker->publishedIssues.first().labels.contains(id));
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("issueJira"))->text().contains(key));
        QVERIFY(f.window->findChild<QWidget*>(QStringLiteral("issueJiraPending"))->isHidden());
        // Publicado, el tag ofrece lo que se puede hacer con el issue del gestor y ya no ofrece crearlo.
        QVERIFY(f.action("issueOpenJira")->isVisible());
        QVERIFY(f.action("issueRefreshJira")->isVisible());
        QVERIFY(f.action("issueUnlinkJira")->isVisible());
        QVERIFY(!f.action("issuePublish")->isVisible());

        // Cambiar el título deja el issue pendiente; actualizar reescribe el del gestor.
        auto* title = f.window->findChild<QLineEdit*>(QStringLiteral("issueTitle"));
        title->selectAll();
        QTest::keyClicks(title, "Laboratorio de merceologia");
        QTest::keyClick(title, Qt::Key_Return);
        QVERIFY(!f.window->findChild<QWidget*>(QStringLiteral("issueJiraPending"))->isHidden());
        f.window->findChild<QPushButton*>(QStringLiteral("issueUpdateJira"))->click();
        auto* update = f.window->findChild<JiraPublishDialog*>();
        QVERIFY(update);
        update->findChild<QPushButton*>(QStringLiteral("jiraPublishAccept"))->click();
        QTRY_VERIFY(!f.window->findChild<JiraPublishDialog*>());
        QCOMPARE(f.app.tracker->updatedKeys, QStringList{key});
        QCOMPARE(f.app.tracker->updatedIssues.first().summary, QStringLiteral("Laboratorio de merceologia"));
        QVERIFY(f.window->findChild<QWidget*>(QStringLiteral("issueJiraPending"))->isHidden());
    }

    // Zephyr se activa en Ajustes y sólo se ofrece con Jira, que es donde vive el plugin.
    // Terminada la revisión, «Publicar…» lleva de una vez las pruebas a Zephyr, el resultado y el acta al
    // gestor y el registro a GESREQ.
    void publishingAFinishedRevisionSendsItToItsThreeDestinations() {
        WindowFixture f;
        f.app.settings.updateTracker([](TrackerSettings& t) { t.zephyr = true; });
        f.app.settings.updateRequirementSource([](RequirementSourceSettings& r) {
            r.url = QStringLiteral("http://gesreq.test:7401/greq");
            r.user = QStringLiteral("jmaidana");
            r.password = QStringLiteral("secreto");
            r.connected = true;
        });
        ExternalRequirement requirement;
        requirement.id = QStringLiteral("2026997");
        requirement.systemCode = QStringLiteral("SUMA2");
        requirement.system = QStringLiteral("SUMA2-INGRESO");
        requirement.summary = QStringLiteral("Integración de nuevos servicios");
        requirement.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
        f.app.issues.importRequirements({requirement}, QStringLiteral("http://gesreq.test:7401/greq"));
        const QString id = f.app.issues.issues().first().id;
        f.app.issues.updateIssue(id, [](Issue& i) {
            i.publication.tracker = QStringLiteral("Jira");
            i.publication.key = QStringLiteral("SHOP-12");
            i.publication.publishedAt = QDateTime::currentDateTime();
        });
        const QString planId = f.app.plans.createPlan(QStringLiteral("Plan GREQ 2026997"));
        f.app.plans.toggle(QStringLiteral("TC-101"));
        f.app.issues.linkPlan(id, planId);
        f.app.issues.openRevision(id);
        f.app.run.startSequence({QStringLiteral("TC-101")}, QStringLiteral("Plan GREQ 2026997"), planId);
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);
        f.app.run.finish();

        f.window->navigate(Screen::Issues);
        // Los pasos de la revisión se rehacen en cada refresco: el botón se busca cuando se va a usar.
        auto publishButton = [&f] { return f.window->findChild<QPushButton*>(QStringLiteral("issuePublishRevision")); };
        QVERIFY(!publishButton()->isEnabled());   // la revisión sigue abierta: primero se cierra

        f.app.issues.closeRevision(id, QaOutcome::Conforme);
        QTRY_VERIFY(publishButton() && publishButton()->isEnabled());
        publishButton()->click();
        auto* dialog = f.window->findChild<RevisionPublishDialog*>();
        QVERIFY(dialog);
        for (const auto* name : {"revisionPublishZephyr", "revisionPublishTracker", "revisionPublishRequirement"}) {
            auto* choice = dialog->findChild<QCheckBox*>(QString::fromLatin1(name));
            QVERIFY2(choice && choice->isChecked(), name);
        }
        dialog->findChild<QPushButton*>(QStringLiteral("revisionPublishAccept"))->click();

        QCOMPARE(f.app.zephyr->published.size(), 1);
        QCOMPARE(f.app.tracker->commentedKeys, QStringList{QStringLiteral("SHOP-12")});
        QCOMPARE(f.app.requirementSource->registrations.size(), 1);
        QCOMPARE(f.app.requirementSource->registrations.first().result, QStringLiteral("Conforme"));
        const IssueRevision& revision = f.app.issues.find(id)->revisions.last();
        QVERIFY(revision.gesreq.registeredAt.isValid());
        QVERIFY(!revision.jira.isEmpty());
        dialog->close();
    }

    // Cerrada la revisión como observada, el trabajo sigue: lo siguiente es el acta y publicar el
    // resultado (no continuar lo fallado, que es de una ronda abierta), y publicado se ve desde el issue
    // que GESREQ lo recibió con la observación y que los ciclos llegaron a Zephyr.
    void anObservedRevisionContinuesWithTheRecordAndThePublication() {
        WindowFixture f;
        f.app.settings.updateTracker([](TrackerSettings& t) { t.zephyr = true; });
        f.app.settings.updateRequirementSource([](RequirementSourceSettings& r) {
            r.url = QStringLiteral("http://gesreq.test:7401/greq");
            r.user = QStringLiteral("jmaidana");
            r.password = QStringLiteral("secreto");
            r.connected = true;
        });
        ExternalRequirement requirement;
        requirement.id = QStringLiteral("2026997");
        requirement.systemCode = QStringLiteral("SUMA2");
        requirement.summary = QStringLiteral("Integración de nuevos servicios");
        requirement.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
        f.app.issues.importRequirements({requirement}, QStringLiteral("http://gesreq.test:7401/greq"));
        const QString id = f.app.issues.issues().first().id;
        f.app.issues.updateIssue(id, [](Issue& i) {
            i.publication.tracker = QStringLiteral("Jira");
            i.publication.key = QStringLiteral("SHOP-12");
            i.publication.publishedAt = QDateTime::currentDateTime();
        });
        const QString planId = f.app.plans.createPlan(QStringLiteral("Plan GREQ 2026997"));
        f.app.plans.toggle(QStringLiteral("TC-101"));
        f.app.issues.linkPlan(id, planId);
        f.app.issues.openRevision(id);
        f.app.run.startSequence({QStringLiteral("TC-101")}, QStringLiteral("Plan GREQ 2026997"), planId);
        const QString firstCycle = f.app.run.planRunId();
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Fail);   // deja el caso fallado
        f.app.run.finish();
        f.app.issues.select(id);

        f.window->navigate(Screen::Issues);
        // Con la ronda abierta, lo que toca es repetir lo que se rompió.
        QCOMPARE(f.liveLabel("issueNextTitle")->text(), QStringLiteral("Continuar lo fallado"));

        // Cerrada como observada, el issue sigue en «fallido / bloqueado» pero lo siguiente ya es el acta.
        f.app.issues.closeRevision(id, QaOutcome::Observado);
        QTRY_COMPARE(f.liveLabel("issueNextTitle")->text(), QStringLiteral("Generar el acta (R-213)"));

        // Sin acta se puede publicar igual: el acta se adjunta si la hay.
        auto publishButton = [&f] { return f.window->findChild<QPushButton*>(QStringLiteral("issuePublishRevision")); };
        QTRY_VERIFY(publishButton() && publishButton()->isEnabled());
        publishButton()->click();
        auto* dialog = f.window->findChild<RevisionPublishDialog*>();
        QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QComboBox*>(QStringLiteral("revisionPublishOutcome"))->currentText(), QStringLiteral("Observado"));
        dialog->findChild<QPushButton*>(QStringLiteral("revisionPublishAccept"))->click();
        dialog->close();

        QCOMPARE(f.app.requirementSource->registrations.first().result, QStringLiteral("Observado"));
        const IssueRevision& revision = f.app.issues.find(id)->revisions.last();
        QVERIFY(revision.gesreq.registeredAt.isValid());
        QCOMPARE(revision.gesreq.requirementState, QStringLiteral("CONTROL DE CALIDAD OBSERVADO"));

        // Y el paso de publicar lo cuenta: cada destino con lo que quedó en él.
        const QString detail = f.liveLabel("issueStepPublishDetail")->text();
        QVERIFY2(detail.contains(QStringLiteral("GESREQ (Observado)")), qPrintable(detail));
        QVERIFY2(detail.contains(QStringLiteral("ZEPHYR")), qPrintable(detail));
        // El acta sigue sin levantarse, así que eso es lo que se propone; publicar y cerrar la revisión
        // están hechos y así se ven, aunque el paso de antes siga pendiente.
        QTRY_COMPARE(f.liveLabel("issueNextTitle")->text(), QStringLiteral("Generar el acta (R-213)"));
        QVERIFY2(f.liveLabel("issueStepPublishDetail")->text().startsWith(QStringLiteral("Publicado en")),
                 qPrintable(f.liveLabel("issueStepPublishDetail")->text()));

        // Y al lado de lo que toca se ofrece volver a probar sólo lo que se rompió: eso abre la revisión
        // siguiente y el ciclo nuevo es de ella, no de la ronda ya cerrada.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        auto* retry = f.window->findChild<QPushButton*>(QStringLiteral("issueNextAlso"));
        QVERIFY(retry);
        QVERIFY2(retry->text().contains(QStringLiteral("Continuar lo fallado")), qPrintable(retry->text()));
        retry->click();
        QVERIFY(f.answerCycleDialog());
        QCOMPARE(f.window->currentScreen(), Screen::Run);
        const PlanRun* cycle = f.app.history.findPlan(f.app.run.planRunId());
        QVERIFY(cycle);
        QCOMPARE(cycle->continuesCycleId, firstCycle);
        // Que ese ciclo sea ya de la revisión siguiente lo pone en pie `ProjectSession` (esta ventana
        // monta los servicios a mano): lo comprueba `startingAPlanCycleStampsTheIssueAndItsRevisionOnIt`.
    }

    // Una ronda que se cerró sin publicar se termina desde su fila del historial: el issue ya está
    // probando la siguiente y aun así lo de la anterior llega a su sitio.
    void aPreviousRevisionIsFinishedFromTheHistory() {
        WindowFixture f;
        f.app.settings.updateRequirementSource([](RequirementSourceSettings& r) {
            r.url = QStringLiteral("http://gesreq.test:7401/greq");
            r.user = QStringLiteral("jmaidana");
            r.password = QStringLiteral("secreto");
            r.connected = true;
        });
        ExternalRequirement requirement;
        requirement.id = QStringLiteral("2026997");
        requirement.systemCode = QStringLiteral("SUMA2");
        requirement.summary = QStringLiteral("Integración de nuevos servicios");
        requirement.states = {QStringLiteral("CONTROL CALIDAD ASIGNADO")};
        f.app.issues.importRequirements({requirement}, QStringLiteral("http://gesreq.test:7401/greq"));
        const QString id = f.app.issues.issues().first().id;
        f.app.issues.updateIssue(id, [](Issue& i) {
            i.publication.tracker = QStringLiteral("Jira");
            i.publication.key = QStringLiteral("SHOP-12");
            i.publication.publishedAt = QDateTime::currentDateTime();
        });
        const QString planId = f.app.plans.createPlan(QStringLiteral("Plan GREQ 2026997"));
        f.app.plans.toggle(QStringLiteral("TC-101"));
        f.app.issues.linkPlan(id, planId);
        f.app.issues.openRevision(id);
        f.app.run.startSequence({QStringLiteral("TC-101")}, QStringLiteral("Plan GREQ 2026997"), planId);
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Fail);
        f.app.run.finish();
        // La ronda 1 se cierra observada y, sin publicarla, el requerimiento vuelve a pruebas.
        f.app.issues.closeRevision(id, QaOutcome::Observado);
        f.app.issues.openRevision(id);

        f.window->navigate(Screen::Issues);
        auto finish = [&f] { return f.window->findChild<QPushButton*>(QStringLiteral("issueRevisionPublish-1")); };
        QTRY_VERIFY(finish());
        QVERIFY2(finish()->toolTip().contains(QStringLiteral("GESREQ")), qPrintable(finish()->toolTip()));
        finish()->click();

        auto* dialog = f.window->findChild<RevisionPublishDialog*>();
        QVERIFY(dialog);
        QVERIFY2(dialog->windowTitle().contains(QStringLiteral("1")), qPrintable(dialog->windowTitle()));
        dialog->findChild<QPushButton*>(QStringLiteral("revisionPublishAccept"))->click();

        // Lo publicado es de la ronda 1, y la 2 sigue abierta y sin tocar.
        const Issue* issue = f.app.issues.find(id);
        QCOMPARE(issue->revisions.size(), 2);
        QVERIFY(issue->revisions.first().gesreq.registeredAt.isValid());
        QVERIFY(!issue->revisions.first().jira.isEmpty());
        QVERIFY(issue->revisions.last().gesreq.isEmpty());
        QVERIFY(issue->currentRevision() != nullptr);
        QCOMPARE(f.app.requirementSource->registrations.first().result, QStringLiteral("Observado"));
        dialog->close();
    }

    // Las pantallas se construyen la primera vez que se entra en ellas: abrir un proyecto no paga las
    // seis vistas, que es lo que hacía que cambiar de proyecto bloqueara la interfaz casi un segundo.
    void screensAreBuiltTheFirstTimeTheyAreOpened() {
        WindowFixture f;
        QVERIFY(f.window->findChild<CasesView*>());        // la pantalla en la que arranca, sí
        QVERIFY(!f.window->findChild<IssuesView*>());
        QVERIFY(!f.window->findChild<HistoryView*>());

        f.window->navigate(Screen::Issues);
        QVERIFY(f.window->findChild<IssuesView*>());
        QVERIFY(!f.window->findChild<HistoryView*>());     // las que no se han abierto siguen sin construirse

        // Y lo que se abre en profundidad también la construye: nadie tiene que saber si ya existía.
        f.window->showMetrics();
        QVERIFY(f.window->findChild<HistoryView*>());
        QCOMPARE(f.window->currentScreen(), Screen::Historial);
    }

    // Mientras se abre otro proyecto, el selector lo dice y no acepta otro cambio.
    void theProjectSelectorSaysWhenAnotherProjectIsOpening() {
        WindowFixture f;
        auto* loading = f.window->findChild<QWidget*>(QStringLiteral("projectLoading"));
        QVERIFY(loading && loading->isHidden());
        auto* selector = f.window->findChild<QComboBox*>(QStringLiteral("projectSelector"));
        QVERIFY(selector);

        f.window->setSwitchingProject(true);
        QVERIFY(!loading->isHidden());
        QVERIFY(!selector->isEnabled());
        QVERIFY2(selector->toolTip().contains(QStringLiteral("abriendo")), qPrintable(selector->toolTip()));

        f.window->setSwitchingProject(false);
        QVERIFY(loading->isHidden());
        QVERIFY(!selector->toolTip().contains(QStringLiteral("abriendo")));
    }

    void settingsEnableZephyrPublishing() {
        WindowFixture f;
        f.action("actSettings")->trigger();
        QWidget* dialog = f.window->settingsWindow();
        auto* zephyr = dialog->findChild<QCheckBox*>(QStringLiteral("settingsZephyr"));
        QVERIFY(zephyr);
        QVERIFY(zephyr->isVisible());
        QVERIFY(!f.app.settings.tracker().zephyr);
        QVERIFY(!f.app.publish.enabled());

        zephyr->setChecked(true);
        QVERIFY(f.app.settings.tracker().zephyr);
        QVERIFY(f.app.publish.enabled());

        // Con otro gestor el bloque desaparece: Zephyr es un plugin de Jira.
        auto* kind = dialog->findChild<QComboBox*>(QStringLiteral("settingsKind"));
        QVERIFY(kind);
        kind->setCurrentText(toString(TrackerKind::GitHub));
        QVERIFY(!zephyr->isVisible());
        QVERIFY(!f.app.publish.enabled());
    }

    void theBackButtonUndoesDrillDownsAndRailNavigation() {
        WindowFixture f;
        auto* back = f.window->findChild<QPushButton*>(QStringLiteral("navbarBack"));
        QVERIFY(back);
        QVERIFY(!back->isVisible());   // en la raíz de una sección no hay a dónde volver

        f.app.plans.setName(QStringLiteral("Suite de regresión"));
        f.window->navigate(Screen::Plan);
        QVERIFY(!back->isVisible());

        // Crear un caso desde el plan lleva a la pantalla de casos, pero con vuelta al plan.
        f.window->findChild<QPushButton*>(QStringLiteral("planNewCase"))->click();
        QCOMPARE(f.window->currentScreen(), Screen::Casos);
        QVERIFY(back->isVisible());
        QVERIFY2(back->text().contains(QStringLiteral("Suite de regresión")), qPrintable(back->text()));
        QVERIFY(f.action("actBack")->isEnabled());
        back->click();
        QCOMPARE(f.window->currentScreen(), Screen::Plan);
        QVERIFY(!back->isVisible());

        // Saltar con el rail también deja vuelta a la pantalla de la que se salió.
        QTest::mouseClick(f.nav(Screen::Historial), Qt::LeftButton);
        QCOMPARE(f.window->currentScreen(), Screen::Historial);
        QVERIFY(back->isVisible());
        QVERIFY2(back->text().contains(QStringLiteral("Suite de regresión")), qPrintable(back->text()));
        QVERIFY(f.action("actBack")->isEnabled());
        back->click();
        QCOMPARE(f.window->currentScreen(), Screen::Plan);
        QVERIFY(!back->isVisible());

        // Pulsar el botón de la pantalla en la que ya se está no alarga el camino.
        QTest::mouseClick(f.nav(Screen::Plan), Qt::LeftButton);
        QVERIFY(!back->isVisible());

        // El menú y los atajos siguen yendo a la raíz de la sección: vacían el camino.
        QTest::mouseClick(f.nav(Screen::Bug), Qt::LeftButton);
        QVERIFY(back->isVisible());
        f.window->navigate(Screen::Historial);
        QVERIFY(!back->isVisible());
        QVERIFY(!f.action("actBack")->isEnabled());

        // Ir y venir entre dos pantallas deshace el camino en vez de alargarlo.
        f.window->navigateInto(Screen::Casos);
        f.window->navigateInto(Screen::Historial);
        QVERIFY(!back->isVisible());
        QCOMPARE(f.window->currentScreen(), Screen::Historial);
    }

    // El ciclo se arranca desde el propio issue: es el paso siguiente del control de calidad y no hay
    // que salir a la pantalla del plan a buscarlo.
    void theIssueScreenStartsTheCycleOfItsPlan() {
        WindowFixture f;
        const QString issueId = f.app.issues.createIssue(QStringLiteral("Alta de clientes"));
        const QString planId = f.app.plans.activeId();
        f.app.issues.linkPlan(issueId, planId);
        f.app.issues.select(issueId);
        f.window->navigate(Screen::Issues);

        // Los pasos de la revisión se rehacen en cada refresco: el botón se busca cuando se va a usar.
        auto runButton = [&f] { return f.window->findChild<QPushButton*>(QStringLiteral("issueStepRun")); };
        QTRY_VERIFY(runButton() && runButton()->isEnabled());
        runButton()->click();
        QVERIFY(f.answerCycleDialog(QStringLiteral("Staging")));

        // Arrancó el ciclo del plan del issue y la ventana lleva a la ejecución.
        QCOMPARE(f.window->currentScreen(), Screen::Run);
        const QString planRunId = f.app.run.planRunId();
        QVERIFY(!planRunId.isEmpty());
        const PlanRun* cycle = f.app.history.findPlan(planRunId);
        QVERIFY(cycle);
        QCOMPARE(cycle->planId, planId);
        QCOMPARE(cycle->environment, QStringLiteral("Staging"));
        QCOMPARE(f.app.run.state().caseId, f.app.plans.orderedCaseIds(planId).first());
    }

    void finishingThePlanCycleOfAnIssueReturnsToTheIssue() {
        WindowFixture f;
        const QString issueId = f.app.issues.createIssue(QStringLiteral("Alta de clientes"));
        const QString planId = f.app.plans.activeId();
        f.app.issues.linkPlan(issueId, planId);

        f.app.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Plan del issue"), planId);
        const QString planRunId = f.app.run.planRunId();
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);
        f.window->navigate(Screen::Run);
        f.window->finishRun();

        // El ciclo prueba un requerimiento: se termina en su issue, no en el informe del plan.
        QCOMPARE(f.window->currentScreen(), Screen::Issues);
        QCOMPARE(f.app.issues.selectedId(), issueId);
        QVERIFY2(f.toastText().contains(QStringLiteral("Plan terminado")), qPrintable(f.toastText()));

        // Y el informe queda a un clic en el aviso, con vuelta al issue.
        auto* seeReport = f.toast()->findChild<QPushButton*>();
        QVERIFY(seeReport);
        QCOMPARE(seeReport->text(), QStringLiteral("Ver informe"));
        seeReport->click();
        QCOMPARE(f.window->currentScreen(), Screen::Historial);
        auto* back = f.window->findChild<QPushButton*>(QStringLiteral("navbarBack"));
        QVERIFY(back->isVisible());
        back->click();
        QCOMPARE(f.window->currentScreen(), Screen::Issues);
        QVERIFY(!planRunId.isEmpty());
    }

    void thePlanReportListsTheBugsReportedDuringTheCycle() {
        WindowFixture f;
        f.app.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"), f.app.plans.activeId());
        const QString planRunId = f.app.run.planRunId();

        // Un bug de antes del ciclo, que no es suyo, y dos reportados mientras corría: probando salen
        // errores y también mejoras, y el informe tiene que enseñar las dos cosas.
        const auto bug = [&](const QString& key, const QString& caseId, int step, const QDateTime& at, bool resolved,
                             const QString& issueType = QStringLiteral("Bug")) {
            IssueLink link;
            link.key = key; link.caseId = caseId; link.step = step; link.createdAt = at; link.resolved = resolved;
            link.title = QStringLiteral("Fallo de ") + caseId;
            link.severity = QStringLiteral("Mayor");
            link.issueType = issueType;
            link.url = QStringLiteral("https://acme.atlassian.net/browse/") + key;
            f.app.bugLedger.recordIssue(link);
        };
        bug(QStringLiteral("SHOP-90"), QStringLiteral("TC-103"), 1, QDateTime::currentDateTime().addDays(-3), false);
        bug(QStringLiteral("SHOP-11"), QStringLiteral("TC-103"), 2, QDateTime::currentDateTime(), false);
        bug(QStringLiteral("SHOP-12"), QStringLiteral("TC-107"), 1, QDateTime::currentDateTime(), true,
            QStringLiteral("Improvement"));

        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Fail);
        f.app.run.finish();
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);
        f.window->finishRun();
        f.window->navigate(Screen::Historial);
        auto* history = f.window->findChild<HistoryView*>();
        QVERIFY(history);
        history->showPlan(planRunId);
        QTest::qWait(50);

        // El resumen los cuenta y el informe los lista, con el de antes del ciclo fuera.
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("openBug-SHOP-11")));
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("openBug-SHOP-12")));
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("openBug-SHOP-90")));
        const PlanReport report = f.app.history.report(planRunId);
        QCOMPARE(report.bugCount(), 2);
        QCOMPARE(report.openBugCount(), 1);

        // Errores y mejoras, cada uno con su cuenta: el informe no los mete a todos en el mismo saco.
        auto* asError = f.window->findChild<QLabel*>(QStringLiteral("bugType-Bug"));
        auto* asImprovement = f.window->findChild<QLabel*>(QStringLiteral("bugType-Improvement"));
        QVERIFY2(asError && asImprovement, "el informe cuenta los hallazgos por tipo");
        QCOMPARE(asError->text(), QStringLiteral("BUG · 1"));
        QCOMPARE(asImprovement->text(), QStringLiteral("IMPROVEMENT · 1"));

        // Y abrirlo lleva al gestor.
        QString opened;
        connect(history, &HistoryView::openUrlRequested, this, [&opened](const QString& url) { opened = url; });
        f.window->findChild<QPushButton*>(QStringLiteral("openBug-SHOP-11"))->click();
        QCOMPARE(opened, QStringLiteral("https://acme.atlassian.net/browse/SHOP-11"));
    }

    // Un requerimiento observado no está terminado: su tarjeta va a «Fallido / bloqueado» hasta que se
    // vuelva a probar, y sólo cerrarlo conforme lo lleva a «Finalizado».
    void anObservedRevisionLeavesTheIssueInTheBrokenColumn() {
        WindowFixture f;
        const QString observed = f.app.issues.createIssue(QStringLiteral("Alta de clientes"));
        const QString accepted = f.app.issues.createIssue(QStringLiteral("Exportar pedidos"));
        for (const auto& id : {observed, accepted}) f.app.issues.openRevision(id);
        f.app.issues.closeRevision(observed, QaOutcome::Observado);
        f.app.issues.closeRevision(accepted, QaOutcome::Conforme);
        QVERIFY(f.app.issues.find(observed)->state == IssueState::Testing);
        QVERIFY(f.app.issues.find(accepted)->state == IssueState::Done);

        f.window->navigate(Screen::Issues);
        const auto columnOf = [&f](const QString& id) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto* card = f.window->findChild<QPushButton*>(QStringLiteral("issueRow-") + id);
            for (QWidget* w = card; w; w = w->parentWidget())
                if (w->objectName().startsWith(QStringLiteral("issueBoardColumn-"))) return w->objectName();
            return QString();
        };
        const QString broken = QStringLiteral("issueBoardColumn-%1").arg(static_cast<int>(IssuesView::Column::Broken));
        const QString done = QStringLiteral("issueBoardColumn-%1").arg(static_cast<int>(IssuesView::Column::Done));
        QCOMPARE(columnOf(observed), broken);
        QCOMPARE(columnOf(accepted), done);

        // Aunque se marque a mano como finalizado (o venga así de antes), sigue siendo observado.
        f.app.issues.updateIssue(observed, [](Issue& i) { i.state = IssueState::Done; });
        QTRY_COMPARE(columnOf(observed), broken);

        // Volver a probarlo abre la revisión siguiente y lo saca de ahí.
        f.app.issues.openRevision(observed);
        QTRY_VERIFY(columnOf(observed) != broken);
    }

    // El tablero de issues reparte por cómo va el trabajo de QA, con una columna para lo que dejó casos
    // fallados o bloqueados; su panel propone continuarlo y el issue entero se abre con doble clic.
    void theIssueBoardHasAColumnForBrokenCasesAndProposesContinuingThem() {
        WindowFixture f;
        const QString fresh = f.app.issues.createIssue(QStringLiteral("Exportar pedidos"));
        const QString issueId = f.app.issues.createIssue(QStringLiteral("Alta de clientes"));
        const QString planId = f.app.plans.activeId();
        f.app.issues.linkPlan(issueId, planId);
        f.app.issues.openRevision(issueId);
        f.app.issues.updateIssue(issueId, [](Issue& i) { i.state = IssueState::Testing; });

        f.app.run.startSequence({QStringLiteral("TC-101"), QStringLiteral("TC-102")}, QStringLiteral("Regresión"), planId,
                                QStringLiteral("QA"));
        const QString planRunId = f.app.run.planRunId();
        f.app.history.noteCycleRevision(planRunId, issueId, 1);
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);   // TC-101 entero
        f.app.run.finish();
        f.app.run.mark(StepResult::Pass);                                       // TC-102, paso 1
        f.app.run.mark(StepResult::Fail);                                       // TC-102, paso 2
        f.window->finishRun();
        f.app.issues.select(issueId);
        f.window->navigate(Screen::Issues);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

        const auto columnOf = [&f](const QString& id) {
            auto* card = f.window->findChild<QPushButton*>(QStringLiteral("issueRow-") + id);
            for (QWidget* w = card; w; w = w->parentWidget())
                if (w->objectName().startsWith(QStringLiteral("issueBoardColumn-"))) return w->objectName();
            return QString();
        };
        QCOMPARE(columnOf(fresh), QStringLiteral("issueBoardColumn-0"));
        QCOMPARE(columnOf(issueId), QStringLiteral("issueBoardColumn-3"));

        // El panel del issue elegido propone continuar lo fallado, y hacerlo arranca la continuación.
        QCOMPARE(f.liveLabel("issueNextTitle")->text(), QStringLiteral("Continuar lo fallado"));
        auto* back = f.window->findChild<QPushButton*>(QStringLiteral("issuesBack"));
        QVERIFY(!back->isVisible());
        QTest::mouseDClick(f.window->findChild<QPushButton*>(QStringLiteral("issueRow-") + issueId), Qt::LeftButton);
        QVERIFY(back->isVisible());
        back->click();
        QVERIFY(!back->isVisible());

        f.window->findChild<QPushButton*>(QStringLiteral("issueNextAction"))->click();
        QVERIFY(f.answerCycleDialog(QStringLiteral("QA")));
        QCOMPARE(f.window->currentScreen(), Screen::Run);
        const PlanRun* cycle = f.app.history.findPlan(f.app.run.planRunId());
        QVERIFY(cycle);
        QCOMPARE(cycle->continuesCycleId, planRunId);
    }

    // La tarjeta del issue se maneja sin salir del tablero: su botón hace lo siguiente que toca, su menú
    // cambia el estado de QA y soltarla en otra columna también (menos en la de fallidos, que es calculada).
    void theIssueCardActsFromTheBoard() {
        WindowFixture f;
        const QString issueId = f.app.issues.createIssue(QStringLiteral("Exportar pedidos"));
        f.app.issues.select(issueId);
        f.window->navigate(Screen::Issues);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto card = [&f, &issueId] { return f.window->findChild<QPushButton*>(QStringLiteral("issueRow-") + issueId); };

        // Sin plan, lo siguiente es crearlo, y el botón de la tarjeta elegida lo hace.
        auto* quick = f.window->findChild<QPushButton*>(QStringLiteral("issueCardAction-") + issueId);
        QVERIFY(quick);
        QCOMPARE(card()->childAt(quick->mapTo(card(), quick->rect().center())), quick);
        QCOMPARE(quick->text(), QStringLiteral("Crear plan"));
        quick->click();
        QCOMPARE(f.app.issues.find(issueId)->planIds.size(), 1);
        QCOMPARE(f.window->currentScreen(), Screen::Plan);

        // El menú de la tarjeta cambia el estado de QA.
        f.window->navigate(Screen::Issues);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        emit card()->customContextMenuRequested(QPoint(4, 4));
        auto* menu = f.window->findChild<QMenu*>(QStringLiteral("issueCardMenu"));
        QVERIFY(menu);
        menu->findChild<QAction*>(QStringLiteral("issueMenuState-%1").arg(static_cast<int>(IssueState::Testing)))->trigger();
        QVERIFY(f.app.issues.find(issueId)->state == IssueState::Testing);
        menu->close();

        // El «⋯» de la tarjeta abre el mismo menú, también en una tarjeta que aún no estaba elegida.
        const QString other = f.app.issues.createIssue(QStringLiteral("Cambiar avatar"));
        f.app.issues.select(issueId);
        QCoreApplication::processEvents();   // las tarjetas nuevas se enseñan en diferido
        QTest::qWait(50);
        auto* otherCard = f.window->findChild<QPushButton*>(QStringLiteral("issueRow-") + other);
        QEvent enter(QEvent::Enter);
        QCoreApplication::sendEvent(otherCard, &enter);
        auto* more = f.window->findChild<QPushButton*>(QStringLiteral("issueCardMore-") + other);
        QVERIFY(more && more->isVisible());
        // Un clic en su sitio le llega a él, no a la tarjeta (un padre transparente al ratón lo taparía).
        QCOMPARE(otherCard->childAt(more->mapTo(otherCard, more->rect().center())), more);
        QTest::mouseClick(more, Qt::LeftButton);
        QTRY_VERIFY(f.window->findChild<QMenu*>(QStringLiteral("issueCardMenu")) &&
                    f.window->findChild<QMenu*>(QStringLiteral("issueCardMenu"))->isVisible());
        QCOMPARE(f.app.issues.selectedId(), other);
        f.window->findChild<QMenu*>(QStringLiteral("issueCardMenu"))->close();
        QTest::qWait(50);   // el panel se rehízo al elegir el otro issue y se enseña en diferido

        // Y el panel del issue abierto tiene el suyo.
        auto* drawerMore = f.window->findChild<QPushButton*>(QStringLiteral("issueDrawerMore"));
        QVERIFY(drawerMore && drawerMore->isVisible());
        QTest::mouseClick(drawerMore, Qt::LeftButton);
        QTRY_VERIFY(f.window->findChild<QMenu*>(QStringLiteral("issueCardMenu")) &&
                    f.window->findChild<QMenu*>(QStringLiteral("issueCardMenu"))->isVisible());
        f.window->findChild<QMenu*>(QStringLiteral("issueCardMenu"))->close();
        f.app.issues.select(issueId);

        // Soltarla en «Finalizado» la finaliza; en la de fallidos no hace nada.
        const auto drop = [&f, &issueId](int column) {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            auto* well = f.window->findChild<QWidget*>(QStringLiteral("issueBoardColumn-%1").arg(column));
            QMimeData mime;
            mime.setData(QByteArrayLiteral("application/x-qaflow-issue"), issueId.toUtf8());
            QDragEnterEvent enter(QPoint(10, 10), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(well, &enter);
            QDropEvent event(QPointF(10, 10), Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(well, &event);
            QCoreApplication::processEvents();
        };
        drop(static_cast<int>(IssuesView::Column::Broken));
        QVERIFY(f.app.issues.find(issueId)->state == IssueState::Testing);
        drop(static_cast<int>(IssuesView::Column::Done));
        QTRY_VERIFY(f.app.issues.find(issueId)->state == IssueState::Done);
    }

    // Lo que falló o quedó bloqueado se retoma desde el informe: el ciclo nuevo repite sólo esos casos,
    // cuelga del anterior y la pantalla de ejecución dice que se está continuando la revisión.
    void theReportContinuesTheCycleWithItsBrokenCases() {
        WindowFixture f;
        const QString issueId = f.app.issues.createIssue(QStringLiteral("Alta de clientes"));
        const QString planId = f.app.plans.activeId();
        f.app.issues.linkPlan(issueId, planId);
        f.app.issues.openRevision(issueId);

        f.app.run.startSequence({QStringLiteral("TC-101"), QStringLiteral("TC-102")}, QStringLiteral("Regresión"), planId,
                                QStringLiteral("QA"));
        const QString planRunId = f.app.run.planRunId();
        f.app.history.noteCycleRevision(planRunId, issueId, 1);
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);   // TC-101 entero
        f.app.run.finish();
        f.app.run.mark(StepResult::Pass);                                       // TC-102, paso 1
        f.app.run.mark(StepResult::Fail);                                       // TC-102, paso 2
        f.window->finishRun();
        f.window->navigate(Screen::Historial);
        auto* history = f.window->findChild<HistoryView*>();
        QVERIFY(history);
        history->showPlan(planRunId);
        QTest::qWait(50);

        auto* proceed = f.window->findChild<QPushButton*>(QStringLiteral("continueCycle"));
        QVERIFY(proceed);
        proceed->click();
        QVERIFY(f.answerCycleDialog(QStringLiteral("QA")));

        // Arrancó la continuación: mismo issue y misma revisión, sólo con el caso roto.
        QCOMPARE(f.window->currentScreen(), Screen::Run);
        const PlanRun* cycle = f.app.history.findPlan(f.app.run.planRunId());
        QVERIFY(cycle);
        QCOMPARE(cycle->continuesCycleId, planRunId);
        QCOMPARE(cycle->caseIds, QStringList{QStringLiteral("TC-102")});
        QCOMPARE(cycle->revision, 1);
        QCOMPARE(cycle->issueId, issueId);

        // Y la ejecución lo dice, con el caso retomado en el paso que se rompió.
        auto* banner = f.window->findChild<QWidget*>(QStringLiteral("runContinuation"));
        QVERIFY(banner);
        QVERIFY(banner->isVisible());
        const QString text = banner->findChild<QLabel*>()->text();
        QVERIFY2(text.contains(QStringLiteral("CONTINUANDO")) && text.contains(planRunId), qPrintable(text));
        QCOMPARE(f.app.run.state().idx, 1);
        QVERIFY(f.app.run.state().results[0].inherited);
    }

    // La columna de la derecha tiene dos pestañas: las capturas y los bugs de la ejecución, los dos
    // por paso. Un bug de otra ejecución del mismo caso no es de ésta. La ficha de un bug se abre en
    // su propia ventana.
    void theRunListsItsBugsByStepAndOpensEachOneInItsOwnWindow() {
        WindowFixture f;
        // Un bug de una ejecución anterior del mismo caso: se quedó en aquellos resultados.
        IssueLink old;
        old.key = QStringLiteral("SHOP-70");
        old.caseId = QStringLiteral("TC-102");
        old.runId = QStringLiteral("R-0099");   // otra ejecución, no la que se va a arrancar
        old.step = 1;
        old.title = QStringLiteral("De otra ejecución");
        old.createdAt = QDateTime::currentDateTime();
        f.app.bugLedger.recordIssue(old);

        f.app.run.start(QStringLiteral("TC-102"));
        IssueLink link;
        link.key = QStringLiteral("SHOP-77");
        link.caseId = QStringLiteral("TC-102");
        link.runId = f.app.run.state().runId;   // el bug sale de esta ejecución
        link.step = 2;
        link.title = QStringLiteral("El cupón no descuenta");
        link.severity = QStringLiteral("Mayor");
        link.classification = QStringLiteral("A");
        link.url = QStringLiteral("https://acme.atlassian.net/browse/SHOP-77");
        link.createdAt = QDateTime::currentDateTime();
        QVERIFY(!link.runId.isEmpty());
        f.app.bugLedger.recordIssue(link);

        f.window->navigate(Screen::Run);
        auto* bugsTab = f.window->findChild<QPushButton*>(QStringLiteral("runBugsTab"));
        QVERIFY(bugsTab);
        QCOMPARE(bugsTab->text(), QStringLiteral("Bugs · 1"));
        bugsTab->click();
        QVERIFY2(!f.window->findChild<QPushButton*>(QStringLiteral("runBug-SHOP-70")),
                 "el bug de otra ejecución no es de ésta");

        auto* card = f.window->findChild<QPushButton*>(QStringLiteral("runBug-SHOP-77"));
        QVERIFY(card);
        card->click();
        auto* window = f.window->findChild<BugDetailWindow*>();
        QVERIFY(window);
        QCOMPARE(window->bugKey(), QStringLiteral("SHOP-77"));
        QVERIFY(window->windowTitle().contains(QStringLiteral("SHOP-77")));

        // Y desde la ficha se va al gestor.
        QString opened;
        connect(window, &BugDetailWindow::openUrlRequested, this, [&opened](const QString& url) { opened = url; });
        window->findChild<QPushButton*>(QStringLiteral("bugDetailOpen"))->click();
        QCOMPARE(opened, QStringLiteral("https://acme.atlassian.net/browse/SHOP-77"));
    }

    void finishingAPlanCycleWithoutAnIssueStillOpensItsReport() {
        WindowFixture f;
        f.app.run.startSequence({QStringLiteral("TC-103")}, QStringLiteral("Plan suelto"), f.app.plans.activeId());
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);
        f.window->finishRun();
        QCOMPARE(f.window->currentScreen(), Screen::Historial);
        QVERIFY(f.toastText().contains(QStringLiteral("Plan terminado")));
    }
};

QTEST_MAIN(MainWindowTest)
#include "test_main_window.moc"
