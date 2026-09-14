#include "application/ProjectStore.h"
#include "application/PlanStore.h"
#include "application/TestCaseStore.h"
#include "application/RunHistoryStore.h"
#include "infrastructure/persistence/JsonProjectRepository.h"
#include "infrastructure/persistence/JsonTestCaseRepository.h"
#include "infrastructure/persistence/JsonRunHistoryRepository.h"
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace qaflow;
class ProjectRepositoryTest : public QObject {
    Q_OBJECT
private slots:
    void legacyFilesStayInTheMainProjectAndNewProjectsStartEmpty() {
        QTemporaryDir dir;
        JsonTestCaseRepository legacy(dir.path());
        TestCase c; c.id = QStringLiteral("TC-1"); c.title = QStringLiteral("Caso existente"); c.suite = QStringLiteral("Suite heredada");
        QVERIFY(legacy.saveCases({c}));
        QFile file(QDir(dir.path()).filePath(QStringLiteral("cases.json")));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray original = file.readAll(); file.close();
        auto repo = std::make_shared<JsonProjectRepository>(dir.path());
        ProjectStore projects(repo);
        QVERIFY(projects.load());
        QCOMPARE(projects.activeId(), QStringLiteral("default"));
        QVERIFY(projects.suites().contains(QStringLiteral("Suite heredada")));
        QCOMPARE(projects.dataDir(projects.activeId()), dir.path());
        const QString id = projects.create(QStringLiteral("  Segundo proyecto  "));
        QVERIFY(!id.isEmpty());
        QCOMPARE(projects.find(id)->name, QStringLiteral("Segundo proyecto"));
        auto casesRepo = std::make_shared<JsonTestCaseRepository>(projects.dataDir(id));
        TestCaseStore cases(casesRepo);
        RunHistoryStore history(std::make_shared<JsonRunHistoryRepository>(projects.dataDir(id)), cases);
        PlanStore plans(casesRepo, cases, history);
        cases.load(); history.load(); plans.load();
        QVERIFY(cases.cases().isEmpty());
        QVERIFY(plans.plans().isEmpty());
        QVERIFY(history.runs().isEmpty());
        QVERIFY(history.plans().isEmpty());
        const QString caseId = cases.createCase();
        cases.updateCase(caseId, [](TestCase& item) { item.suite = QStringLiteral("Suite de proyecto sin abrir"); });
        QVERIFY(cases.save());
        QVERIFY(projects.rename(id, QStringLiteral("Renombrado")));
        QVERIFY(projects.setActive(id));
        ProjectStore restored(repo);
        QVERIFY(restored.load());
        QCOMPARE(restored.activeId(), id);
        QVERIFY(restored.suites().contains(QStringLiteral("Suite de proyecto sin abrir")));
        cases.updateCase(caseId, [](TestCase& item) { item.suite.clear(); });
        QVERIFY(cases.save());
        ProjectStore afterRemoval(repo);
        QVERIFY(afterRemoval.load());
        QVERIFY(afterRemoval.suites().contains(QStringLiteral("Suite de proyecto sin abrir")));
        QCOMPARE(restored.find(id)->name, QStringLiteral("Renombrado"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original);
        QCOMPARE(legacy.loadCases()->size(), 1);
        QCOMPARE(legacy.loadCases()->first().title, c.title);
    }
    // El sistema de GESREQ es del proyecto, se guarda en el catálogo y no puede repetirse entre proyectos:
    // al iniciar las pruebas de un requerimiento tiene que haber un único proyecto al que ir.
    void aRequirementSystemBelongsToASingleProject() {
        QTemporaryDir dir;
        auto repo = std::make_shared<JsonProjectRepository>(dir.path());
        ProjectStore projects(repo);
        QVERIFY(projects.load());
        const QString main = projects.activeId();
        const QString transit = projects.create(QStringLiteral("Tránsitos"));
        QVERIFY(projects.find(transit)->requirementSystem.isEmpty());

        QVERIFY(projects.setRequirementSystem(transit, QStringLiteral("  SUMA   TRANSITO ")));
        QCOMPARE(projects.find(transit)->requirementSystem, QStringLiteral("SUMA TRANSITO"));
        QCOMPARE(projects.projectForRequirementSystem(QStringLiteral("suma transito")), transit);
        QVERIFY(projects.projectForRequirementSystem(QStringLiteral("SUMA TRANSITO"), transit).isEmpty());   // salvo él mismo
        QVERIFY(projects.projectForRequirementSystem(QString()).isEmpty());

        QSignalSpy failures(&projects, &ProjectStore::failed);
        QVERIFY(!projects.setRequirementSystem(main, QStringLiteral("Suma Transito")));
        QCOMPARE(failures.count(), 1);
        QVERIFY2(failures.first().first().toString().contains(QStringLiteral("Tránsitos")), qPrintable(failures.first().first().toString()));
        QVERIFY(projects.find(main)->requirementSystem.isEmpty());

        ProjectStore restored(repo);
        QVERIFY(restored.load());
        QCOMPARE(restored.find(transit)->requirementSystem, QStringLiteral("SUMA TRANSITO"));
        QVERIFY(restored.find(main)->requirementSystem.isEmpty());
        QVERIFY(restored.setRequirementSystem(transit, QString()));
        QVERIFY(restored.setRequirementSystem(main, QStringLiteral("SUMA TRANSITO")));   // liberado, ya puede usarlo otro
    }

    void invalidCatalogIsNotOverwritten() {
        QTemporaryDir dir;
        QFile file(QDir(dir.path()).filePath(QStringLiteral("projects.json")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bad = R"({"version":1,"activeId":"../outside","projects":[{"id":"../outside","name":"Invalid"}]})";
        file.write(bad); file.close();
        JsonProjectRepository repo(dir.path());
        QVERIFY(!repo.load());
        QVERIFY(repo.dataDir(QStringLiteral("../outside")).isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bad);
    }
    void failedWritesDoNotChangeTheActiveProject() {
        QTemporaryDir dir;
        auto repo = std::make_shared<JsonProjectRepository>(dir.path());
        ProjectStore projects(repo);
        QVERIFY(projects.load());
        const QString id = projects.create(QStringLiteral("Segundo"));
        QVERIFY(!id.isEmpty());
        const QString path = QDir(dir.path()).filePath(QStringLiteral("projects.json"));
        QVERIFY(QFile::remove(path));
        QVERIFY(QDir().mkdir(path)); // Hace fallar QSaveFile sin depender de permisos de root.
        QVERIFY(!projects.setActive(id));
        QCOMPARE(projects.activeId(), QStringLiteral("default"));
        QVERIFY(!projects.rename(id, QStringLiteral("No guardado")));
        QCOMPARE(projects.find(id)->name, QStringLiteral("Segundo"));
        QVERIFY(projects.create(QStringLiteral("No guardado")).isEmpty());
        QCOMPARE(projects.projects().size(), 2);
    }
};
QTEST_GUILESS_MAIN(ProjectRepositoryTest)
#include "test_project_repository.moc"
