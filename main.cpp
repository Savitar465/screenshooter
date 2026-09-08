// QAflow · punto de entrada y raíz de composición.
// Aquí se eligen las implementaciones concretas de infraestructura y se inyectan
// en los servicios de aplicación; las vistas reciben sólo el AppContext.
//
// La ventana principal se construye con el idioma y el tema activos; cambiar cualquiera de los dos
// (SettingsStore::appChanged) la reconstruye en el mismo sitio y pantalla.

#include "application/AppContext.h"
#include "infrastructure/capture/GifRecorder.h"
#include "infrastructure/capture/ScreenCaptureService.h"
#include "infrastructure/hotkey/GlobalHotkey.h"
#include "infrastructure/persistence/JsonBugRepository.h"
#include "infrastructure/persistence/JsonRunHistoryRepository.h"
#include "infrastructure/persistence/JsonRunSessionRepository.h"
#include "infrastructure/persistence/JsonTestCaseRepository.h"
#include "infrastructure/persistence/QSettingsRepository.h"
#include "infrastructure/secrets/SecretStores.h"
#include "infrastructure/tracker/TrackerRouter.h"
#include "presentation/DevSnapshot.h"
#include "presentation/theme/Theme.h"
#include "presentation/views/MainWindow.h"

#include <QApplication>
#include <QFont>
#include <QLibraryInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTimer>
#include <QTranslator>

#include <memory>

namespace {

#ifndef QAFLOW_VERSION
#define QAFLOW_VERSION "0.5.0"
#endif

/// Idioma efectivo: el elegido o, con "sistema", el del entorno (español si el sistema es español).
QLocale localeFor(qaflow::AppLanguage lang) {
    switch (lang) {
        case qaflow::AppLanguage::Spanish: return QLocale(QLocale::Spanish);
        case qaflow::AppLanguage::English: return QLocale(QLocale::English);
        case qaflow::AppLanguage::System: return QLocale::system();
    }
    return QLocale::system();
}

struct Translators {
    QTranslator app;
    QTranslator qt;
};

void applyLanguage(QApplication& app, Translators& tr, qaflow::AppLanguage lang) {
    app.removeTranslator(&tr.app);
    app.removeTranslator(&tr.qt);
    const QLocale locale = localeFor(lang);
    QLocale::setDefault(locale);
    // Las cadenas del código están en español; sólo hace falta cargar algo para otros idiomas.
    if (locale.language() != QLocale::Spanish) {
        if (tr.app.load(locale, QStringLiteral("qaflow"), QStringLiteral("_"), QStringLiteral(":/i18n"))) app.installTranslator(&tr.app);
    }
    if (tr.qt.load(locale, QStringLiteral("qtbase"), QStringLiteral("_"), QLibraryInfo::path(QLibraryInfo::TranslationsPath))) app.installTranslator(&tr.qt);
}

void applyTheme(QApplication& app, qaflow::AppTheme theme) {
    qaflow::theme::apply(qaflow::theme::paletteFor(theme));
    app.setStyleSheet(qaflow::theme::stylesheet());
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("QAflow"));
    QApplication::setOrganizationName(QStringLiteral("QAflow"));
    QApplication::setApplicationVersion(QStringLiteral(QAFLOW_VERSION));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QFont f = app.font();
    f.setFamilies({QStringLiteral("Segoe UI"), QStringLiteral("Inter"), QStringLiteral("Noto Sans"), QStringLiteral("DejaVu Sans"), QStringLiteral("sans-serif")});
    f.setPixelSize(13);
    app.setFont(f);

    using namespace qaflow;

    // Infraestructura
    const QString dataDir = devsnapshot::requested() ? devsnapshot::dataDir()
                                                     : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (devsnapshot::requested()) devsnapshot::isolateSettings();   // ajustes aparte: no toca ~/.config
    auto caseRepo = std::make_shared<JsonTestCaseRepository>(dataDir);
    auto historyRepo = std::make_shared<JsonRunHistoryRepository>(dataDir);
    auto sessionRepo = std::make_shared<JsonRunSessionRepository>(dataDir);
    auto settingsRepo = std::make_shared<QSettingsRepository>();
    auto secrets = makeSecretStore();   // llavero del sistema si lo hay; si no, avisa en Ajustes
    auto bugRepo = std::make_shared<JsonBugRepository>(dataDir);
    auto tracker = std::make_shared<TrackerRouter>();   // Jira, GitHub, GitLab o Azure DevOps según Ajustes
    auto capture = std::make_shared<ScreenCaptureService>();   // grabWindow o portal de Wayland
    auto recorder = std::make_shared<GifRecorder>();
    GlobalHotkey hotkey;                                       // atajo del sistema (RegisterHotKey, XGrabKey, portal, Carbon)

    // Aplicación
    TestCaseStore cases(caseRepo);
    SettingsStore settings(settingsRepo, secrets);
    RunHistoryStore history(historyRepo, cases);
    RunController run(cases, history, sessionRepo);
    PlanStore plan(caseRepo, cases, history);
    BugStore bugLedger(bugRepo);
    BugReportService bugs(tracker, cases, run, settings, bugLedger);
    EvidenceService evidence(capture, cases, run, settings);
    evidence.setRecorder(recorder);
    CaseTransferService transfer(cases);
    settings.load();   // primero: idioma y tema deciden cómo se construye todo lo demás
    if (devsnapshot::requested()) devsnapshot::applyRequestedAppSettings(settings);
    Translators translators;
    applyLanguage(app, translators, settings.app().language);
    applyTheme(app, settings.app().theme);
    cases.load();
    bugLedger.load();
    plan.load();
    history.load();
    run.load();   // ejecución interrumpida en la sesión anterior, si la hay

    AppContext ctx;
    ctx.cases = &cases;
    ctx.plan = &plan;
    ctx.run = &run;
    ctx.history = &history;
    ctx.settings = &settings;
    ctx.bugs = &bugs;
    ctx.bugLedger = &bugLedger;
    ctx.evidence = &evidence;
    ctx.transfer = &transfer;
    ctx.hotkey = &hotkey;
    ctx.dataDir = dataDir;
    ctx.captureBackend = capture->backendName();

    // Atajos globales: capturar y grabar aunque la ventana no tenga el foco. Si el sistema los
    // rechaza, siguen funcionando los QAction de la ventana (mismo atajo, ámbito aplicación).
    auto bindHotkeys = [&]() {
        const CaptureSettings& c = settings.capture();
        if (!c.globalShortcut) { hotkey.unbind(QStringLiteral("capture")); hotkey.unbind(QStringLiteral("record")); return; }
        hotkey.bind(QStringLiteral("capture"), c.shortcut, [&evidence]() { evidence.captureForSelectedCase(); });
        hotkey.bind(QStringLiteral("record"), c.recordShortcut, [&evidence]() { evidence.toggleRecording(); });
    };
    bindHotkeys();
    QObject::connect(&settings, &SettingsStore::captureChanged, &app, bindHotkeys);

    // Presentación (la captura oculta la ventana principal mientras captura)
    std::unique_ptr<MainWindow> window;
    auto buildWindow = [&]() {
        std::unique_ptr<MainWindow> previous = std::move(window);
        window = std::make_unique<MainWindow>(ctx);
        capture->setAppWindow(window.get());
        recorder->setAppWindow(window.get());
        if (previous) {
            window->setGeometry(previous->geometry());
            window->navigate(previous->currentScreen());
        }
        window->show();
        // La ventana de ajustes es hija de la principal: si estaba abierta (por ejemplo porque el
        // cambio de idioma o de tema salió de ella), se vuelve a abrir con la ventana nueva.
        if (previous && previous->settingsWindow()) window->openSettings();
        previous.reset();
    };
    buildWindow();

    // Idioma o tema nuevos: reconstruir la ventana (diferido, porque la señal llega desde un widget suyo).
    AppLanguage lastLanguage = settings.app().language;
    AppTheme lastTheme = settings.app().theme;
    QObject::connect(&settings, &SettingsStore::appChanged, &app, [&]() {
        const AppSettings& a = settings.app();
        if (a.language == lastLanguage && a.theme == lastTheme) return;
        lastLanguage = a.language;
        lastTheme = a.theme;
        QTimer::singleShot(0, &app, [&]() {
            applyLanguage(app, translators, settings.app().language);
            applyTheme(app, settings.app().theme);
            buildWindow();
        });
    });
    // Con icono en la bandeja la app no termina al ocultar la ventana; «Salir» llama a quit().
    app.setQuitOnLastWindowClosed(!settings.app().closeToTray);
    QObject::connect(&settings, &SettingsStore::appChanged, &app, [&]() { app.setQuitOnLastWindowClosed(!settings.app().closeToTray); });

    if (devsnapshot::requested()) devsnapshot::run(*window, ctx);

    return app.exec();
}
