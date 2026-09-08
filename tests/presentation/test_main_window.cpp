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
#include "presentation/widgets/ImageViewer.h"
#include "presentation/widgets/Thumbnail.h"
#include "presentation/widgets/Toast.h"

#include <QAction>
#include <QComboBox>
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
        window = std::make_unique<MainWindow>(ctx);
        window->show();
        QApplication::setActiveWindow(window.get());
        QVERIFY(QTest::qWaitForWindowExposed(window.get()));
    }
    QAction* action(const char* name) const { return window->findChild<QAction*>(QString::fromLatin1(name)); }
    QPushButton* nav(Screen s) const { return window->findChild<QPushButton*>(QStringLiteral("nav-%1").arg(static_cast<int>(s))); }
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

    void captureActionAttachesScreenshotToSelectedCase() {
        WindowFixture f;
        const QString id = f.app.store.selectedId();
        f.action("actCapture")->trigger();
        QTRY_COMPARE(f.app.store.find(id)->shots.size(), 1);
        QVERIFY(QFile::exists(f.app.store.find(id)->shots[0].path));
        QTRY_VERIFY(f.toast()->isVisible());
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
};

QTEST_MAIN(MainWindowTest)
#include "test_main_window.moc"
