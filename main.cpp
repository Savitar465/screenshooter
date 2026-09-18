// QAflow · punto de entrada y raíz de composición.
// Aquí se eligen las implementaciones concretas de infraestructura y se inyectan
// en los servicios de aplicación; las vistas reciben sólo el AppContext.
//
// La ventana principal se construye con el idioma y el tema activos; cambiar cualquiera de los dos
// (SettingsStore::appChanged) la reconstruye en el mismo sitio y pantalla.

#include "bootstrap/ProjectSession.h"
#include "infrastructure/hotkey/GlobalHotkey.h"
#include "infrastructure/persistence/JsonProjectRepository.h"
#include "infrastructure/requirements/GesreqClient.h"
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
#include <optional>
#include <QMessageBox>

namespace {

#ifndef QAFLOW_VERSION
#define QAFLOW_VERSION "1.1.0"
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
    // Una sola sesión de GESREQ para todos los proyectos: la bandeja es del usuario, no del proyecto.
    auto requirementSource = std::make_shared<GesreqClient>();
    GlobalHotkey hotkey;
    Translators translators;
    // Las sesiones conservan sus servicios y ventanas al cambiar de proyecto. Los callbacks
    // pendientes siempre terminan en el proyecto que inició la operación.
    WorkspaceWindow workspace;
    std::map<QString, std::unique_ptr<ProjectSession>> sessions;
    ProjectSession* current = nullptr;
    // Un cambio de proyecto a la vez: mientras dura, la aplicación está «cargando» y no acepta otro.
    bool switching = false;
    // Idioma y tema ya aplicados. Volver a ponerlos reescribe la hoja de estilos y Qt repinta el árbol
    // entero de las dos ventanas, así que sólo se hace cuando de verdad cambian.
    std::optional<AppLanguage> appliedLanguage;
    std::optional<AppTheme> appliedTheme;
    // Cambiar de proyecto ya no termina cuando la función vuelve (se hace en pasos), así que lo que
    // tenga que pasar **después** se pide aquí: `then` corre sólo si el cambio llegó a hacerse.
    std::function<void(const QString&, std::function<void()>)> switchProject;
    std::function<void(ProjectSession&)> buildWindow;
    // Se declara antes porque las ventanas que construye `buildWindow` ya piden la sesión de otro proyecto.
    std::function<ProjectSession&(const QString&)> getSession;

    auto applyLook = [&](ProjectSession& session) {
        const AppSettings& a = session.settings->app();
        if (appliedLanguage != a.language) {
            applyLanguage(app, translators, a.language);
            appliedLanguage = a.language;
        }
        if (appliedTheme != a.theme) {
            applyTheme(app, a.theme);
            appliedTheme = a.theme;
        }
    };

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
            QTimer::singleShot(0, &app, [&, id]() { switchProject(id, {}); });
        });
        // Iniciar las pruebas de un requerimiento de otro proyecto: se guarda el actual, se activa el suyo
        // y allí se abre (o se crea) su issue. Si el cambio no llegó a hacerse, no se inicia nada.
        QObject::connect(session.window.get(), &MainWindow::startTestingRequested, &app,
                         [&, owner = &session](const QString& id, const ExternalRequirement& requirement,
                                               const QString& connection, const QDateTime& fetchedAt) {
            if (owner != current) return;
            QTimer::singleShot(0, &app, [&, id, requirement, connection, fetchedAt]() {
                switchProject(id, [&, id, requirement, connection, fetchedAt]() {
                    if (!current || current->ctx.projectId != id) return;
                    current->window->startTesting(requirement, connection, fetchedAt);
                });
            });
        });
        // El código Jira de un proyecto vive en sus ajustes, que sólo tiene abiertos su sesión: al crearlo
        // desde otra ventana se guarda aquí, antes de que el cambio de proyecto los recargue.
        QObject::connect(session.window.get(), &MainWindow::projectJiraKeyRequested, &app,
                         [&, owner = &session](const QString& id, const QString& key) {
            if (owner != current || key.isEmpty() || !projects.find(id)) return;
            getSession(id).settings->updateTracker([&key](TrackerSettings& s) {
                if (s.kind == TrackerKind::Jira) s.project = key;
            });
        });
        workspace.showProject(session.window.get());
        if (reopenSettings) session.window->openSettings();
    };

    getSession = [&](const QString& id) -> ProjectSession& {
        auto& stored = sessions[id];
        if (stored) return *stored;
        stored = std::make_unique<ProjectSession>(projects, id, secrets, requirementSource);
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

    // Abrir un proyecto por primera vez cuesta lo suyo (sus datos y, sobre todo, construir su ventana) y
    // todo eso ocurre en el hilo de la interfaz. Así que el cambio se anuncia —la aplicación se pone en
    // «cargando»— y se hace **en pasos**, volviendo al bucle de eventos entre uno y otro: sin eso el
    // aviso no llegaría a pintarse y el indicador no se movería.
    switchProject = [&](const QString& id, std::function<void()> then) {
        if (switching) return;   // ya hay uno en marcha: el de ahora todavía está a medias
        if (!projects.find(id) || (current && current->ctx.projectId == id)) return;
        if (current) {
            QString reason;
            if (!current->canLeave(&reason)) {
                current->window->showToast(reason, theme::Amber);
                return;
            }
            // Si no se pudo guardar, el cambio se cancela: los avisos de los stores dicen qué falló.
            if (!current->save()) return;
        }
        const Project* target = projects.find(id);
        const QString name = target ? target->name : id;
        MainWindow* from = current ? current->window.get() : nullptr;
        switching = true;
        if (from) from->setSwitchingProject(true);
        workspace.setBusy(true, QObject::tr("Abriendo «%1»…").arg(name),
                          QObject::tr("Preparando sus casos, planes e issues"));
        // Se llama termine como termine el cambio, también si se queda a medias.
        auto finish = [&, from]() {
            switching = false;
            if (from) from->setSwitchingProject(false);
            workspace.setBusy(false);
        };

        // Paso 1 · los datos del proyecto: sus casos, planes, historial, bugs e issues.
        QTimer::singleShot(0, &app, [&, id, finish, then]() {
            auto& next = getSession(id);
            if (!projects.setActive(id)) { finish(); return; }
            // Recargar los ajustes generales compartidos y el código Jira del proyecto destino.
            next.settings->load();
            current = &next;
            applyLook(*current);

            // Paso 2 · su ventana, que es lo caro y sólo se paga la primera vez.
            QTimer::singleShot(0, &app, [&, finish, then]() {
                if (!current->window
                    || current->window->property("uiLanguage").toInt() != static_cast<int>(current->settings->app().language)
                    || current->window->property("uiTheme").toInt() != static_cast<int>(current->settings->app().theme))
                    buildWindow(*current);

                // Paso 3 · enseñarla y devolver la aplicación a su estado normal.
                QTimer::singleShot(0, &app, [&, finish, then]() {
                    workspace.showProject(current->window.get());
                    bindHotkeys();
                    app.setQuitOnLastWindowClosed(!current->settings->app().closeToTray);
                    finish();
                    // Hecho el cambio, lo que lo pidió sigue su camino (abrir el issue del requerimiento
                    // que se iba a probar, por ejemplo), ya con el proyecto destino activo.
                    if (then) then();
                });
            });
        });
    };

    current = &getSession(projects.activeId());
    if (devsnapshot::requested()) devsnapshot::applyRequestedAppSettings(*current->settings);
    applyLook(*current);
    buildWindow(*current);
    workspace.show();
    bindHotkeys();
    app.setQuitOnLastWindowClosed(!current->settings->app().closeToTray);
    if (devsnapshot::requested()) devsnapshot::run(*current->window, current->ctx);
    return app.exec();
}
