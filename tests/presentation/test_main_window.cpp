// MainWindow (presentation/views/MainWindow.h) con toda la capa de aplicación sobre repositorios
// en memoria y una captura de pantalla falsa. Se ejecuta con la plataforma "offscreen".
// Cubre: navegación (sidebar, menú, atajos), acciones de menú, teclas de la ejecución, filtros de
// la lista de casos, métricas y el aviso con «Reintentar» cuando falla el guardado.

#include "support/AppFixture.h"
#include "support/FakeScreenRecorder.h"

#include "application/AppContext.h"
#include "application/CaseTransferService.h"
#include "application/EvidenceService.h"
#include "presentation/views/CycleStartDialog.h"
#include "presentation/views/HistoryView.h"
#include "presentation/views/MainWindow.h"
#include "presentation/views/PlanView.h"
#include "presentation/views/JiraPublishDialog.h"
#include "presentation/views/ProjectSetupDialog.h"
#include "presentation/views/RequirementImportDialog.h"
#include "presentation/views/RevisionPublishDialog.h"
#include "presentation/widgets/ChoiceDialog.h"
#include "presentation/widgets/EvidencePreview.h"
#include "presentation/widgets/ImageViewer.h"
#include "presentation/widgets/Thumbnail.h"
#include "presentation/widgets/Toast.h"

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QScrollArea>
#include <QScrollBar>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
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

    // La evidencia de una ejecución se ve en su ficha del historial, y desde ahí se abre el visor.
    void clickingAThumbnailOpensTheViewer() {
        WindowFixture f;
        const QString id = f.app.store.selectedId();
        f.app.run.start(id);
        f.action("actCapture")->trigger();
        QTRY_COMPARE(f.app.store.find(id)->shots.size(), 1);
        while (f.app.run.isRunning()) f.app.run.mark(StepResult::Pass);
        f.app.run.finish();

        auto* history = f.window->findChild<HistoryView*>();
        QVERIFY(history);
        f.window->navigate(Screen::Historial);
        history->showRun(f.app.history.runsForCase(id).first().id);
        // Reportar bug también crea tarjetas (ocultas): hay que esperar a la miniatura visible.
        auto visibleThumb = [&]() -> Thumbnail* {
            for (auto* t : f.window->findChildren<Thumbnail*>()) if (t->isVisible()) return t;
            return nullptr;
        };
        QTRY_VERIFY(visibleThumb() != nullptr);
        QTest::mouseClick(visibleThumb(), Qt::LeftButton);
        QTRY_VERIFY(f.window->findChild<ImageViewer*>() != nullptr);
        auto* viewer = f.window->findChild<ImageViewer*>();
        QCOMPARE(viewer->current().fileName, f.app.store.find(id)->shots[0].fileName);
        viewer->close();
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

    /// La lista de pasos de la ejecución es navegable: un clic lleva a ese paso, marcado o no.
    void clickingAStepOfTheRunListGoesToIt() {
        WindowFixture f;
        f.action("actRun")->trigger();   // TC-104, 4 pasos
        f.action("actStepPass")->trigger();
        QCOMPARE(f.app.run.state().idx, 1);

        auto* fourth = f.window->findChild<QFrame*>(QStringLiteral("stepCard4"));
        QVERIFY(fourth);
        QTest::mouseClick(fourth, Qt::LeftButton);
        QCOMPARE(f.app.run.state().idx, 3);
        QCOMPARE(f.app.run.state().markedCount(), 1);   // saltar no marca nada

        auto* first = f.window->findChild<QFrame*>(QStringLiteral("stepCard1"));
        QVERIFY(first);
        QTest::mouseClick(first, Qt::LeftButton);       // volver a uno ya marcado
        QCOMPARE(f.app.run.state().idx, 0);
        QVERIFY(f.app.run.state().isMarked(0));
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
        QCOMPARE(static_cast<int>(f.window->currentScreen()), static_cast<int>(Screen::Bug));
        QCOMPARE(f.window->findChild<QLineEdit*>(QStringLiteral("bugTitle"))->text().contains(QStringLiteral("paso 1")), true);

        f.window->navigate(Screen::Run);
        QVERIFY(f.app.run.isRunning());       // y se puede seguir probando el resto
        QTest::keyClick(f.window.get(), Qt::Key_B);
        QCOMPARE(f.app.run.state().markedCount(), 2);
        QVERIFY(f.app.run.isRunning());
        QCOMPARE(report->text(), QStringLiteral("Reportar bug bloqueante"));
        QTest::mouseClick(report, Qt::LeftButton);
        auto* severity = f.window->findChild<QComboBox*>(QStringLiteral("bugSeverity"));
        QVERIFY(severity);
        QCOMPARE(severity->currentData().toString(), QStringLiteral("Bloqueante"));
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

    /// Con varias capturas, la última cae fuera de la parte visible del carrete: debe traerse a la vista.
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
        QScrollBar* bar = scroll->verticalScrollBar();
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
        f.window->navigate(Screen::Bug);
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
        rec.steps = {RunRecordStep{QStringLiteral("Entrar"), QStringLiteral("Entra"), StepResult::Pass, {}, 30}};
        const RunRecord saved = f.app.history.addRun(rec);
        f.app.history.finishPlan(planRunId);
        f.app.history.markPublished(planRunId, QStringLiteral("77"));

        auto* history = f.window->findChild<HistoryView*>();
        QVERIFY(history);
        // A lo ancho de una pantalla normal, no de la máxima: la cabecera del informe lleva tres
        // botones y es donde se apretaba la línea de la publicación.
        f.window->resize(760, 700);
        f.window->navigate(Screen::Historial);
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

    void thePlanReportOffersDeletingEveryCycleButTheOneInProgress() {
        WindowFixture f;
        auto* history = f.window->findChild<HistoryView*>();
        QVERIFY(history);
        f.window->navigate(Screen::Historial);

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

        // Consultar GESREQ muestra toda la bandeja; la acción unificada importa y abre el elegido.
        f.window->findChild<QPushButton*>(QStringLiteral("issuesConsult"))->click();
        auto* import = f.window->findChild<RequirementImportDialog*>();
        QVERIFY(import);
        auto* candidates = import->findChild<QListWidget*>(QStringLiteral("importList"));
        QCOMPARE(candidates->count(), 2);
        QVERIFY(!(candidates->item(0)->flags() & Qt::ItemIsUserCheckable));
        QVERIFY(!import->findChild<QPushButton*>(QStringLiteral("importAccept")));
        auto* start = import->findChild<QPushButton*>(QStringLiteral("importStartTesting"));
        QVERIFY(start && !start->isEnabled());
        import->accept();
        QVERIFY(f.app.issues.issues().isEmpty());
        candidates->setCurrentRow(0);
        QVERIFY(start->isEnabled());
        const QString summary = import->findChild<QLabel*>(QStringLiteral("importSummary"))->text();
        QVERIFY2(summary.contains(QStringLiteral("1 de otros sistemas")), qPrintable(summary));
        import->findChild<QPushButton*>(QStringLiteral("importStartTesting"))->click();
        QTRY_VERIFY(!f.window->findChild<RequirementImportDialog*>());
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
        f.window->navigate(Screen::Issues);
        // La tarjeta de planes enseña el plan del issue con sus casos dentro.
        const QString plansHeader = f.window->findChild<QLabel*>(QStringLiteral("issuePlansHeader"))->text();
        QVERIFY2(!plansHeader.isEmpty(), qPrintable(plansHeader));
        QVERIFY(!f.window->findChild<QLabel*>(QStringLiteral("issueCasesHeader")));   // ya no hay tarjeta de casos

        // Una ejecución suelta de ese caso no es un resultado del issue; un ciclo de su plan, sí.
        f.app.run.start(QStringLiteral("TC-104"));
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);
        f.app.run.finish();
        f.window->navigate(Screen::Issues);
        QVERIFY(IssueStore::runsOf(*f.app.issues.find(id), f.app.history).isEmpty());
        QVERIFY2(f.window->findChild<QLabel*>(QStringLiteral("issueResultsHeader"))->text().contains(QStringLiteral("0")),
                 "sin ciclos del plan no hay resultados del issue");

        f.app.run.startSequence({QStringLiteral("TC-104")}, QStringLiteral("Plan del issue"), planId);
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);
        f.app.run.finish();
        f.window->navigate(Screen::Issues);
        QCOMPARE(IssueStore::runsOf(*f.app.issues.find(id), f.app.history).size(), 1);
        const QString resultsHeader = f.window->findChild<QLabel*>(QStringLiteral("issueResultsHeader"))->text();
        QVERIFY2(resultsHeader.contains(QStringLiteral("1")), qPrintable(resultsHeader));

        // Lo escrito en QAflow se queda aunque GESREQ cambie; el cambio se avisa hasta revisarlo.
        title->selectAll();
        QTest::keyClicks(title, "Mi titulo");
        QTest::keyClick(title, Qt::Key_Return);
        QCOMPARE(f.app.issues.find(id)->title, QStringLiteral("Mi titulo"));
        mine.states = {QStringLiteral("CONTROL DE CALIDAD OBSERVADO")};
        f.app.requirementSource->inbox = {mine};
        f.window->findChild<QPushButton*>(QStringLiteral("issuesConsult"))->click();
        import = f.window->findChild<RequirementImportDialog*>();
        QVERIFY(import);
        import->findChild<QListWidget*>(QStringLiteral("importList"))->setCurrentRow(0);
        import->findChild<QPushButton*>(QStringLiteral("importStartTesting"))->click();
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

    void consultingGesreqWithoutALinkedSystemOpensTheSettings() {
        WindowFixture f;
        f.window->navigate(Screen::Issues);
        f.window->findChild<QPushButton*>(QStringLiteral("issuesConsult"))->click();
        QVERIFY(f.window->settingsWindow());
        QCOMPARE(f.app.requirementSource->inboxReads, 0);
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
        f.window->navigate(Screen::Issues);
        f.window->findChild<QPushButton*>(QStringLiteral("issuesConsult"))->click();
        auto* import = f.window->findChild<RequirementImportDialog*>();
        QVERIFY(import);
        auto* others = import->findChild<QListWidget*>(QStringLiteral("importList"));
        auto* start = import->findChild<QPushButton*>(QStringLiteral("importStartTesting"));
        QVERIFY(others && start);
        QCOMPARE(others->count(), 3);
        QVERIFY(others->item(0)->text().contains(QStringLiteral("Proyecto actual")));
        QVERIFY(others->item(0)->text().contains(QStringLiteral("2025175")));
        QVERIFY(others->item(1)->text().contains(QStringLiteral("CONTROL CALIDAD ASIGNADO")));
        QVERIFY(!(others->item(1)->flags() & Qt::ItemIsUserCheckable));
        QVERIFY(!(others->item(0)->flags() & Qt::ItemIsUserCheckable));
        QVERIFY(!start->isEnabled());   // sin elegir requerimiento no hay pruebas que empezar

        // El de un sistema que nadie trabaja dice que no tiene proyecto, pero se puede empezar igual:
        // primero se elige o se crea (ver startingTestsOfASystemNobodyWorksAsksForItsProject).
        others->setCurrentRow(2);
        QVERIFY(start->isEnabled());
        QVERIFY2(others->item(2)->text().contains(QStringLiteral("ningún proyecto")), qPrintable(others->item(2)->text()));
        others->setCurrentRow(1);
        QVERIFY(start->isEnabled());
        QVERIFY2(start->text().contains(QStringLiteral("Riesgos")), qPrintable(start->text()));
        start->click();
        QCOMPARE(started.count(), 1);
        QCOMPARE(started.first().at(0).toString(), otherId);
        QCOMPARE(started.first().at(1).value<ExternalRequirement>().id, QStringLiteral("2025719"));
        QVERIFY(f.app.issues.issues().isEmpty());   // el issue es del otro proyecto, no de éste
        QTRY_VERIFY(!f.window->findChild<RequirementImportDialog*>());

        // El del sistema del proyecto se abre aquí, sin pedir cambio de proyecto.
        f.window->findChild<QPushButton*>(QStringLiteral("issuesConsult"))->click();
        import = f.window->findChild<RequirementImportDialog*>();
        QVERIFY(import);
        import->findChild<QListWidget*>(QStringLiteral("importList"))->setCurrentRow(0);
        import->findChild<QPushButton*>(QStringLiteral("importStartTesting"))->click();
        QCOMPARE(started.count(), 1);
        QCOMPARE(f.app.issues.issues().size(), 1);
        const Issue& issue = f.app.issues.issues().first();
        QCOMPARE(issue.requirement.data.id, QStringLiteral("2025175"));
        QCOMPARE(f.app.issues.selectedId(), issue.id);
        QCOMPARE(f.window->currentScreen(), Screen::Issues);
        QTRY_VERIFY(!f.window->findChild<RequirementImportDialog*>());
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

        QSignalSpy started(f.window.get(), &MainWindow::startTestingRequested);
        QSignalSpy jiraKey(f.window.get(), &MainWindow::projectJiraKeyRequested);
        f.window->navigate(Screen::Issues);
        f.window->findChild<QPushButton*>(QStringLiteral("issuesConsult"))->click();
        auto* import = f.window->findChild<RequirementImportDialog*>();
        QVERIFY(import);
        import->findChild<QListWidget*>(QStringLiteral("importList"))->setCurrentRow(0);
        auto* start = import->findChild<QPushButton*>(QStringLiteral("importStartTesting"));
        QVERIFY(start->isEnabled());
        QVERIFY2(start->text().contains(QStringLiteral("Crear proyecto")), qPrintable(start->text()));
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

        QSignalSpy started(f.window.get(), &MainWindow::startTestingRequested);
        f.window->navigate(Screen::Issues);
        f.window->findChild<QPushButton*>(QStringLiteral("issuesConsult"))->click();
        auto* import = f.window->findChild<RequirementImportDialog*>();
        QVERIFY(import);
        import->findChild<QListWidget*>(QStringLiteral("importList"))->setCurrentRow(0);
        import->findChild<QPushButton*>(QStringLiteral("importStartTesting"))->click();

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
        auto* publish = f.window->findChild<QPushButton*>(QStringLiteral("issuePublish"));
        QVERIFY(publish);
        QVERIFY(publish->isEnabled());
        QVERIFY(f.window->findChild<QWidget*>(QStringLiteral("issueJiraPending"))->isHidden());

        publish->click();
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
        QVERIFY(f.window->findChild<QLabel*>(QStringLiteral("issueJira"))->text().contains(key));
        QVERIFY(f.window->findChild<QWidget*>(QStringLiteral("issueJiraPending"))->isHidden());
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("issueOpenJira"))->isHidden());

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

    void theBackButtonUndoesDrillDownsButNotRailNavigation() {
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

        // El rail es la raíz de cada sección: no deja camino que deshacer.
        f.window->findChild<QPushButton*>(QStringLiteral("planNewCase"))->click();
        QVERIFY(back->isVisible());
        QTest::mouseClick(f.nav(Screen::Historial), Qt::LeftButton);
        QVERIFY(!back->isVisible());
        QVERIFY(!f.action("actBack")->isEnabled());

        // Ir y venir entre dos pantallas deshace el camino en vez de alargarlo.
        f.window->navigateInto(Screen::Casos);
        f.window->navigateInto(Screen::Historial);
        QVERIFY(!back->isVisible());
        QCOMPARE(f.window->currentScreen(), Screen::Historial);
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
        auto* history = f.window->findChild<HistoryView*>();
        f.app.run.startSequence({QStringLiteral("TC-103"), QStringLiteral("TC-107")}, QStringLiteral("Regresión"), f.app.plans.activeId());
        const QString planRunId = f.app.run.planRunId();

        // Un bug de antes del ciclo, que no es suyo, y dos reportados mientras corría.
        const auto bug = [&](const QString& key, const QString& caseId, int step, const QDateTime& at, bool resolved) {
            IssueLink link;
            link.key = key; link.caseId = caseId; link.step = step; link.createdAt = at; link.resolved = resolved;
            link.title = QStringLiteral("Fallo de ") + caseId;
            link.severity = QStringLiteral("Mayor");
            link.url = QStringLiteral("https://acme.atlassian.net/browse/") + key;
            f.app.bugLedger.recordIssue(link);
        };
        bug(QStringLiteral("SHOP-90"), QStringLiteral("TC-103"), 1, QDateTime::currentDateTime().addDays(-3), false);
        bug(QStringLiteral("SHOP-11"), QStringLiteral("TC-103"), 2, QDateTime::currentDateTime(), false);
        bug(QStringLiteral("SHOP-12"), QStringLiteral("TC-107"), 1, QDateTime::currentDateTime(), true);

        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Fail);
        f.app.run.finish();
        while (!f.app.run.state().finished) f.app.run.mark(StepResult::Pass);
        f.window->finishRun();
        history->showPlan(planRunId);
        QTest::qWait(50);

        // El resumen los cuenta y el informe los lista, con el de antes del ciclo fuera.
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("openBug-SHOP-11")));
        QVERIFY(f.window->findChild<QPushButton*>(QStringLiteral("openBug-SHOP-12")));
        QVERIFY(!f.window->findChild<QPushButton*>(QStringLiteral("openBug-SHOP-90")));
        const PlanReport report = f.app.history.report(planRunId);
        QCOMPARE(report.bugCount(), 2);
        QCOMPARE(report.openBugCount(), 1);

        // Y abrirlo lleva al gestor.
        QString opened;
        connect(history, &HistoryView::openUrlRequested, this, [&opened](const QString& url) { opened = url; });
        f.window->findChild<QPushButton*>(QStringLiteral("openBug-SHOP-11"))->click();
        QCOMPARE(opened, QStringLiteral("https://acme.atlassian.net/browse/SHOP-11"));
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
