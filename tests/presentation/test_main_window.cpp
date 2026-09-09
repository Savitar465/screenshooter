// MainWindow (presentation/views/MainWindow.h) con toda la capa de aplicación sobre repositorios
// en memoria y una captura de pantalla falsa. Se ejecuta con la plataforma "offscreen".
// Cubre: navegación (sidebar, menú, atajos), acciones de menú, teclas de la ejecución, filtros de
// la lista de casos, métricas y el aviso con «Reintentar» cuando falla el guardado.

#include "support/AppFixture.h"
#include "support/FakeScreenRecorder.h"

#include "application/AppContext.h"
#include "application/CaseTransferService.h"
#include "application/EvidenceService.h"
#include "presentation/views/MainWindow.h"
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
#include <QPushButton>
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

    void droppingFilesAttachesThemToTheSelectedCase() {
        WindowFixture f;
        const QString id = f.app.store.selectedId();
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

    void clickingAThumbnailOpensTheViewer() {
        WindowFixture f;
        f.action("actCapture")->trigger();
        // Reportar bug también crea tarjetas (ocultas): hay que esperar a la miniatura visible de Casos.
        auto visibleThumb = [&]() -> Thumbnail* {
            for (auto* t : f.window->findChildren<Thumbnail*>()) if (t->isVisible()) return t;
            return nullptr;
        };
        QTRY_VERIFY(visibleThumb() != nullptr);
        QTest::mouseClick(visibleThumb(), Qt::LeftButton);
        QTRY_VERIFY(f.window->findChild<ImageViewer*>() != nullptr);
        auto* viewer = f.window->findChild<ImageViewer*>();
        QCOMPARE(viewer->current().fileName, f.app.store.selected()->shots[0].fileName);
        viewer->close();
    }

    void captureCountdownShowsAToastAndCanBeCancelled() {
        WindowFixture f;
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
        QCOMPARE(f.app.run.state().results.size(), 1);
        QTest::keyClick(f.window.get(), Qt::Key_F);
        QCOMPARE(f.app.run.state().results.size(), 2);
        QCOMPARE(static_cast<int>(f.app.run.state().results[1].result), static_cast<int>(StepResult::Fail));
        QTest::keyClick(f.window.get(), Qt::Key_Backspace);
        QCOMPARE(f.app.run.state().results.size(), 1);
    }

    /// Los atajos de la ejecución (los mismos que main.cpp registra en el sistema) avanzan y
    /// retroceden de paso desde el menú, sin pasar por la pantalla.
    void runStepActionsFollowTheSettings() {
        WindowFixture f;
        QCOMPARE(f.action("actStepPass")->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+P")));
        QCOMPARE(f.action("actStepFail")->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+F")));
        QCOMPARE(f.action("actStepBack")->shortcut(), QKeySequence(QStringLiteral("Ctrl+Alt+A")));
        QVERIFY(!f.action("actStepPass")->isEnabled());   // sin ejecución no hacen nada
        QVERIFY(!f.action("actStepBack")->isEnabled());

        f.action("actRun")->trigger();   // TC-104
        QVERIFY(f.action("actStepPass")->isEnabled());
        f.action("actStepPass")->trigger();
        QCOMPARE(f.app.run.state().results.size(), 1);
        QCOMPARE(static_cast<int>(f.app.run.state().results[0].result), static_cast<int>(StepResult::Pass));
        QCOMPARE(f.app.run.state().idx, 1);
        f.action("actStepFail")->trigger();
        QCOMPARE(static_cast<int>(f.app.run.state().results[1].result), static_cast<int>(StepResult::Fail));
        f.action("actStepBack")->trigger();
        QCOMPARE(f.app.run.state().results.size(), 1);
        QCOMPARE(f.app.run.state().idx, 1);

        f.app.settings.updateRunShortcuts([](RunShortcuts& r) { r.passAndNext = QStringLiteral("F8"); });
        QCOMPARE(f.action("actStepPass")->shortcut(), QKeySequence(Qt::Key_F8));
    }

    void captureActionAttachesScreenshotToSelectedCase() {
        WindowFixture f;
        const QString id = f.app.store.selectedId();
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

    // Zephyr se activa en Ajustes y sólo se ofrece con Jira, que es donde vive el plugin.
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
};

QTEST_MAIN(MainWindowTest)
#include "test_main_window.moc"
