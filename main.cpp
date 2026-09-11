// QAflow · punto de entrada y raíz de composición.
// Aquí se eligen las implementaciones concretas de infraestructura y se inyectan
// en los servicios de aplicación; las vistas reciben sólo el AppContext.
//
// La ventana principal se construye con el idioma y el tema activos; cambiar cualquiera de los dos
// (SettingsStore::appChanged) la reconstruye en el mismo sitio y pantalla.

#include "bootstrap/ProjectSession.h"
#include "infrastructure/hotkey/GlobalHotkey.h"
#include "infrastructure/persistence/JsonProjectRepository.h"
#include "infrastructure/secrets/SecretStores.h"
#include "presentation/DevSnapshot.h"
#include "presentation/theme/Theme.h"
#include "presentation/views/WorkspaceWindow.h"

#include <QApplication>
#include <QFont>
#include <QLibraryInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTimer>
#include <QTranslator>

#include <memory>
#include <map>
#include <QMessageBox>

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

    const QString dataDir = devsnapshot::requested() ? devsnapshot::dataDir()
                                                     : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (devsnapshot::requested()) devsnapshot::isolateSettings();
    ProjectStore projects(std::make_shared<JsonProjectRepository>(dataDir));
    if (!projects.load()) {
        QMessageBox::critical(nullptr, QObject::tr("Proyectos"), QObject::tr("No se pudo abrir el catálogo de proyectos. Revisa projects.json y los permisos de la carpeta de datos."));
        return 1;
    }
    auto secrets = makeSecretStore();
    GlobalHotkey hotkey;
    Translators translators;
    // Las sesiones conservan sus servicios y ventanas al cambiar de proyecto. Los callbacks
    // pendientes siempre terminan en el proyecto que inició la operación.
    WorkspaceWindow workspace;
    std::map<QString, std::unique_ptr<ProjectSession>> sessions;
    ProjectSession* current = nullptr;
    std::function<void(const QString&)> switchProject;
    std::function<void(ProjectSession&)> buildWindow;

    auto bindHotkeys = [&]() {
        const CaptureSettings& c = current->settings->capture();
        const RunShortcuts& r = current->settings->runShortcuts();
        for (const char* id : {"capture", "record", "step-pass", "step-fail", "step-back"}) hotkey.unbind(QString::fromLatin1(id));
        if (!c.globalShortcut) return;
        hotkey.bind(QStringLiteral("capture"), c.shortcut, [&]() { current->evidence->captureForSelectedCase(); });
        hotkey.bind(QStringLiteral("record"), c.recordShortcut, [&]() { current->evidence->toggleRecording(); });
        hotkey.bind(QStringLiteral("step-pass"), r.passAndNext, [&]() {
            if (!current->run->isRunning()) return;
            current->run->mark(StepResult::Pass);
            current->window->announceRunStep();
        });
        hotkey.bind(QStringLiteral("step-fail"), r.failAndNext, [&]() {
            if (!current->run->isRunning()) return;
            current->run->mark(StepResult::Fail);
            current->window->announceRunStep();
        });
        hotkey.bind(QStringLiteral("step-back"), r.previous, [&]() {
            if (current->run->state().caseId.isEmpty()) return;
            current->run->back();
            current->window->announceRunStep();
        });
    };

    buildWindow = [&](ProjectSession& session) {
        auto previous = std::move(session.window);
        const bool reopenSettings = previous && previous->settingsWindow();
        session.window = std::make_unique<MainWindow>(session.ctx);
        session.window->setProperty("uiLanguage", static_cast<int>(session.settings->app().language));
        session.window->setProperty("uiTheme", static_cast<int>(session.settings->app().theme));
        session.capture->setAppWindow(&workspace);
        session.recorder->setAppWindow(&workspace);
        if (previous) {
            session.window->navigate(previous->currentScreen());
        }
        QObject::connect(session.window.get(), &MainWindow::projectSwitchRequested, &app, [&, owner = &session](const QString& id) {
            if (owner != current) return;
            QTimer::singleShot(0, &app, [&, id]() { switchProject(id); });
        });
        workspace.showProject(session.window.get());
        if (reopenSettings) session.window->openSettings();
    };

    auto getSession = [&](const QString& id) -> ProjectSession& {
        auto& stored = sessions[id];
        if (stored) return *stored;
        stored = std::make_unique<ProjectSession>(projects, id, secrets);
        auto* session = stored.get();
        session->ctx.hotkey = &hotkey;
        QObject::connect(session->settings.get(), &SettingsStore::captureChanged, &app, [&, session]() { if (current == session) bindHotkeys(); });
        QObject::connect(session->settings.get(), &SettingsStore::runShortcutsChanged, &app, [&, session]() { if (current == session) bindHotkeys(); });
        QObject::connect(session->settings.get(), &SettingsStore::appChanged, &app, [&, session]() {
            if (current != session || !session->window) return;
            app.setQuitOnLastWindowClosed(!session->settings->app().closeToTray);
            const auto& a = session->settings->app();
            if (session->window->property("uiLanguage").toInt() == static_cast<int>(a.language)
                && session->window->property("uiTheme").toInt() == static_cast<int>(a.theme)) return;
            QTimer::singleShot(0, &app, [&, session]() {
                if (current != session) return;
                applyLanguage(app, translators, session->settings->app().language);
                applyTheme(app, session->settings->app().theme);
                buildWindow(*session);
            });
        });
        return *session;
    };

    switchProject = [&](const QString& id) {
        if (!projects.find(id) || (current && current->ctx.projectId == id)) return;
        if (current) {
            if (!current->run->state().caseId.isEmpty() || current->evidence->isRecording() || current->evidence->isCountingDown() || current->evidence->isBusy()) {
                current->window->showToast(QObject::tr("Finaliza o detén la ejecución y las capturas antes de cambiar de proyecto"), theme::Amber);
                return;
            }
            if (!current->save()) return;
        }
        auto& next = getSession(id);
        if (!projects.setActive(id)) return;
        // Recargar los ajustes generales compartidos y el código Jira del proyecto destino.
        next.settings->load();
        current = &next;
        applyLanguage(app, translators, current->settings->app().language);
        applyTheme(app, current->settings->app().theme);
        if (!current->window
            || current->window->property("uiLanguage").toInt() != static_cast<int>(current->settings->app().language)
            || current->window->property("uiTheme").toInt() != static_cast<int>(current->settings->app().theme)) buildWindow(*current);
        workspace.showProject(current->window.get());
        bindHotkeys();
        app.setQuitOnLastWindowClosed(!current->settings->app().closeToTray);
    };

    current = &getSession(projects.activeId());
    if (devsnapshot::requested()) devsnapshot::applyRequestedAppSettings(*current->settings);
    applyLanguage(app, translators, current->settings->app().language);
    applyTheme(app, current->settings->app().theme);
    buildWindow(*current);
    workspace.show();
    bindHotkeys();
    app.setQuitOnLastWindowClosed(!current->settings->app().closeToTray);
    if (devsnapshot::requested()) devsnapshot::run(*current->window, current->ctx);
    return app.exec();
}
