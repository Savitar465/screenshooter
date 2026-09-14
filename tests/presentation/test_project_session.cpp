#include "bootstrap/ProjectSession.h"
#include "presentation/views/WorkspaceWindow.h"
#include "infrastructure/persistence/JsonProjectRepository.h"
#include "support/MemoryRepositories.h"
#include <QDir>
#include <QComboBox>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace qaflow;
class WindowEvents : public QObject {
public:
    int hides = 0;
    int closes = 0;
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::Hide) ++hides;
        if (event->type() == QEvent::Close) ++closes;
        return false;
    }
};

class ProjectSessionTest : public QObject {
    Q_OBJECT
private slots:
    void switchingProjectsKeepsTheSameVisibleNativeWindow() {
        QTemporaryDir dir;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir.path());
        QCoreApplication::setOrganizationName(QStringLiteral("QAflowWindowTest"));
        QCoreApplication::setApplicationName(QStringLiteral("QAflowWindowTest"));
        ProjectStore projects(std::make_shared<JsonProjectRepository>(dir.path()));
        QVERIFY(projects.load());
        const QString first = projects.create(QStringLiteral("A"));
        const QString second = projects.create(QStringLiteral("B"));
        auto secrets = std::make_shared<testing::MemorySecretStore>();
        ProjectSession a(projects, first, secrets), b(projects, second, secrets);
        const QString caseId = b.cases->createCase();
        a.window = std::make_unique<MainWindow>(a.ctx);
        b.window = std::make_unique<MainWindow>(b.ctx);
        WorkspaceWindow frame;
        frame.showProject(a.window.get());
        frame.show();
        QTest::qWait(30);
        const auto nativeId = frame.winId();
        const QRect geometry = frame.geometry();
        const auto state = frame.windowState();
        WindowEvents events;
        frame.installEventFilter(&events);
        a.window->navigate(Screen::Plan);
        frame.showProject(b.window.get());
        QTest::qWait(30);
        QCOMPARE(frame.currentProject(), b.window.get());
        QCOMPARE(frame.winId(), nativeId);
        QCOMPARE(frame.geometry(), geometry);
        QCOMPARE(frame.windowState(), state);
        QVERIFY(frame.isVisible());
        QVERIFY(!a.window->isVisible());
        QVERIFY(b.window->isVisible());
        QCOMPARE(events.hides, 0);
        QCOMPARE(events.closes, 0);
        QApplication::setActiveWindow(&frame);
        QTest::keyClick(b.window.get(), Qt::Key_F5);
        QCOMPARE(b.run->state().caseId, caseId);
        QVERIFY(a.run->state().caseId.isEmpty());
        b.run->abandon();
        frame.showProject(a.window.get());
        QCOMPARE(a.window->currentScreen(), Screen::Plan);
        QCOMPARE(frame.winId(), nativeId);
        QCOMPARE(events.hides, 0);
        QCOMPARE(events.closes, 0);
        frame.close();
        QCOMPARE(events.closes, 1);
        QVERIFY(!frame.isVisible());
    }

    // Criterio de aceptación del flujo "Iniciar pruebas": desde el proyecto A, empezar las pruebas de un
    // requerimiento de B activa B y abre allí su issue, sin mezclar datos, duplicar issues ni perder lo de
    // A. Con una ejecución en curso no se cambia de proyecto.
    void startingTestsOfAnotherProjectActivatesItAndOpensItsIssue() {
        QTemporaryDir dir;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir(dir.path()).filePath(QStringLiteral("config")));
        QCoreApplication::setOrganizationName(QStringLiteral("QAflowStartTest"));
        QCoreApplication::setApplicationName(QStringLiteral("QAflowStartTest"));
        ProjectStore projects(std::make_shared<JsonProjectRepository>(dir.path()));
        QVERIFY(projects.load());
        const QString first = projects.create(QStringLiteral("Tránsito"));
        const QString second = projects.create(QStringLiteral("Riesgos"));
        QVERIFY(projects.setActive(first));
        QVERIFY(projects.setRequirementSystem(second, QStringLiteral("SEGRAN")));
        auto secrets = std::make_shared<testing::MemorySecretStore>();
        const QString connection = QStringLiteral("http://gesreq.test:7401/greq");
        ExternalRequirement requirement;
        requirement.id = QStringLiteral("2025719");
        requirement.system = QStringLiteral("SEGRAN-RIESGOS");
        requirement.systemCode = QStringLiteral("SEGRAN");
        requirement.summary = QStringLiteral("Módulo de riesgos");
        requirement.priority = QStringLiteral("ALTA");

        QString caseId;
        {
            ProjectSession a(projects, first, secrets), b(projects, second, secrets);
            a.window = std::make_unique<MainWindow>(a.ctx);
            b.window = std::make_unique<MainWindow>(b.ctx);
            WorkspaceWindow frame;
            frame.showProject(a.window.get());
            frame.show();
            ProjectSession* current = &a;
            // La misma coordinación que hace main.cpp con las sesiones abiertas.
            auto startTesting = [&](const QString& id, const ExternalRequirement& r, const QString& conn, const QDateTime& at) {
                QString reason;
                if (!current->canLeave(&reason)) return;   // ejecución o captura en curso: no se cambia
                if (!current->save()) return;              // si no se pudo guardar, se cancela el cambio
                QVERIFY(projects.setActive(id));
                current = id == first ? &a : &b;
                frame.showProject(current->window.get());
                current->window->startTesting(r, conn, at);
            };
            QObject::connect(a.window.get(), &MainWindow::startTestingRequested, a.window.get(), startTesting);
            QObject::connect(b.window.get(), &MainWindow::startTestingRequested, b.window.get(), startTesting);

            // Lo que se estaba haciendo en A todavía no está en disco.
            caseId = a.cases->createCase();
            a.cases->updateCase(caseId, [](TestCase& c) {
                c.title = QStringLiteral("Solo A");
                c.steps = {{QStringLiteral("Paso"), QStringLiteral("Resultado")}};
            });

            emit a.window->startTestingRequested(second, requirement, connection, QDateTime::currentDateTime());
            QCOMPARE(projects.activeId(), second);
            QCOMPARE(current, &b);
            QCOMPARE(frame.currentProject(), b.window.get());
            QCOMPARE(b.window->currentScreen(), Screen::Issues);
            QCOMPARE(b.issues->issues().size(), 1);
            const Issue& issue = b.issues->issues().first();
            QCOMPARE(issue.requirement.data.id, QStringLiteral("2025719"));
            QCOMPARE(issue.title, QStringLiteral("Módulo de riesgos"));
            QCOMPARE(b.issues->selectedId(), issue.id);
            QVERIFY(a.issues->issues().isEmpty());   // los issues no se mezclan
            QVERIFY(a.cases->find(caseId));          // lo de A sigue en A

            // Volver a empezarlas reutiliza el mismo issue; con una ejecución en curso no se cambia.
            emit b.window->startTestingRequested(second, requirement, connection, QDateTime::currentDateTime());
            QCOMPARE(b.issues->issues().size(), 1);
            b.window->navigate(Screen::Casos);
            const QString bCase = b.cases->createCase();
            b.cases->updateCase(bCase, [](TestCase& c) { c.steps = {{QStringLiteral("Paso"), QStringLiteral("Resultado")}}; });
            b.run->start(bCase);
            emit b.window->startTestingRequested(first, requirement, connection, QDateTime::currentDateTime());
            QCOMPARE(projects.activeId(), second);
            QCOMPARE(current, &b);
            b.run->abandon();
            QVERIFY(b.save());
        }
        // Al dejar A se guardó lo suyo, y el issue quedó en B.
        ProjectSession a(projects, first, secrets), b(projects, second, secrets);
        QVERIFY(a.cases->find(caseId));
        QCOMPARE(a.cases->find(caseId)->title, QStringLiteral("Solo A"));
        QVERIFY(a.issues->issues().isEmpty());
        QCOMPARE(b.issues->issues().size(), 1);
        QCOMPARE(b.issues->issues().first().requirement.connection, connection);
    }

    void projectsKeepDataAndJiraCodesSeparateButShareSettingsAndSuites() {
        QTemporaryDir dir;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QDir(dir.path()).filePath(QStringLiteral("config")));
        QCoreApplication::setOrganizationName(QStringLiteral("QAflowProjectTest"));
        QCoreApplication::setApplicationName(QStringLiteral("QAflowProjectTest"));
        auto repo = std::make_shared<JsonProjectRepository>(dir.path());
        ProjectStore projects(repo);
        QVERIFY(projects.load());
        const QString first = projects.create(QStringLiteral("A"));
        const QString second = projects.create(QStringLiteral("B"));
        QVERIFY(!first.isEmpty() && !second.isEmpty());
        auto secrets = std::make_shared<testing::MemorySecretStore>();
        QString caseId, firstFolder;
        {
            ProjectSession a(projects, first, secrets), b(projects, second, secrets);
            QVERIFY(a.cases->cases().isEmpty()); QVERIFY(b.cases->cases().isEmpty());
            QVERIFY(a.plan->plans().isEmpty()); QVERIFY(b.plan->plans().isEmpty());
            a.window = std::make_unique<MainWindow>(a.ctx);
            a.window->show();
            auto* selector = a.window->findChild<QComboBox*>(QStringLiteral("projectSelector"));
            auto* runButton = a.window->findChild<QPushButton*>(QStringLiteral("navbarRun"));
            QVERIFY(selector && runButton);
            QCOMPARE(selector->currentData().toString(), first);
            QVERIFY(selector->isEnabled());
            QVERIFY(!runButton->isEnabled());
            QSignalSpy switches(a.window.get(), &MainWindow::projectSwitchRequested);
            QVERIFY(QMetaObject::invokeMethod(selector, "activated", Q_ARG(int, selector->findData(second))));
            QCOMPARE(switches.count(), 1);
            QCOMPARE(switches.first().first().toString(), second);
            QCOMPARE(selector->currentData().toString(), first); // Espera el cambio coordinado por main.

            caseId = a.cases->createCase();
            QCOMPARE(b.cases->createCase(), caseId); // Los ids locales pueden coincidir.
            a.cases->updateCase(caseId, [](TestCase& c) { c.title = QStringLiteral("Solo A"); c.steps = {{QStringLiteral("Paso"), QStringLiteral("Resultado")}}; });
            b.cases->updateCase(caseId, [](TestCase& c) { c.title = QStringLiteral("Solo B"); });
            a.plan->createPlan(QStringLiteral("Plan A"));
            a.plan->toggle(caseId);
            a.run->start(caseId);
            QVERIFY(!selector->isEnabled());
            QVERIFY(!runButton->isEnabled());
            a.run->mark(StepResult::Pass);
            QVERIFY(!selector->isEnabled()); // El resultado todavía debe archivarse.
            a.run->finish();
            QVERIFY(selector->isEnabled());
            QCOMPARE(a.history->runs().size(), 1); QVERIFY(b.history->runs().isEmpty());
            a.settings->updateTracker([](TrackerSettings& s) { s.project = QStringLiteral("A"); s.token = QStringLiteral("token-a"); s.zephyr = true; });
            QCOMPARE(b.settings->tracker().token, QStringLiteral("token-a"));
            QVERIFY(b.settings->tracker().project.isEmpty());
            QVERIFY(b.settings->tracker().zephyr);
            // GESREQ: la conexión y su contraseña se comparten; el sistema es de cada proyecto y no se repite.
            QVERIFY(a.ctx.requirements && b.ctx.requirements);
            a.settings->updateRequirementSource([](RequirementSourceSettings& r) { r.url = QStringLiteral("http://gesreq.test/greq"); r.password = QStringLiteral("clave"); });
            QCOMPARE(b.settings->requirementSource().url, QStringLiteral("http://gesreq.test/greq"));
            QCOMPARE(b.settings->requirementSource().password, QStringLiteral("clave"));
            QVERIFY(projects.setRequirementSystem(first, QStringLiteral("SUMA TRANSITO")));
            QVERIFY(!projects.setRequirementSystem(second, QStringLiteral("SUMA TRANSITO")));
            b.settings->updateTracker([](TrackerSettings& s) { s.project = QStringLiteral("B"); s.token = QStringLiteral("token-b"); });
            firstFolder = a.settings->capture().folder;
            QCOMPARE(b.settings->capture().folder, firstFolder);
            QVERIFY(a.evidence->captureFolder() != b.evidence->captureFolder());
            a.settings->updateCapture([&](CaptureSettings& c) { c.folder = QDir(dir.path()).filePath(QStringLiteral("shared-captures")); });
            firstFolder = a.settings->capture().folder;
            QCOMPARE(b.settings->capture().folder, firstFolder);
            a.cases->updateCase(caseId, [](TestCase& c) { c.suite = QStringLiteral("Suite común"); });
            QVERIFY(b.cases->suites().contains(QStringLiteral("Suite común")));
            QCOMPARE(b.cases->cases().size(), 1);
            b.cases->updateCase(caseId, [](TestCase& c) { c.suite = QStringLiteral("Suite común"); });
            a.settings->updateApp([](AppSettings& s) { s.theme = AppTheme::Light; });
            QVERIFY(a.save()); QVERIFY(b.save());
        }
        ProjectSession a(projects, first, secrets), b(projects, second, secrets);
        QCOMPARE(a.cases->find(caseId)->title, QStringLiteral("Solo A"));
        QCOMPARE(b.cases->find(caseId)->title, QStringLiteral("Solo B"));
        QCOMPARE(a.history->runs().size(), 1); QVERIFY(b.history->runs().isEmpty());
        QCOMPARE(a.plan->plans().size(), 1); QVERIFY(b.plan->plans().isEmpty());
        QCOMPARE(a.settings->tracker().token, QStringLiteral("token-b"));
        QCOMPARE(b.settings->tracker().token, QStringLiteral("token-b"));
        QCOMPARE(a.settings->tracker().project, QStringLiteral("A"));
        QCOMPARE(b.settings->tracker().project, QStringLiteral("B"));
        QVERIFY(a.settings->tracker().zephyr); QVERIFY(b.settings->tracker().zephyr);
        QCOMPARE(a.settings->capture().folder, firstFolder);
        QCOMPARE(b.settings->capture().folder, firstFolder);
        QVERIFY(a.cases->suites().contains(QStringLiteral("Suite común")));
        QVERIFY(b.cases->suites().contains(QStringLiteral("Suite común")));
        QVERIFY(b.settings->app().theme == AppTheme::Light);
    }
};
QTEST_MAIN(ProjectSessionTest)
#include "test_project_session.moc"
