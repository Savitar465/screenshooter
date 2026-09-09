// TestCaseStore (application/TestCaseStore.h): fuente de verdad de los casos.

#include "support/AppFixture.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;
using qaflow::testing::MemoryTestCaseRepository;

class TestCaseStoreTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Carga y alta ------------------------------------------------------------------

    void loadsSeedWhenRepositoryIsEmpty() {
        auto repo = std::make_shared<MemoryTestCaseRepository>();
        TestCaseStore store(repo);
        store.load();
        QCOMPARE(store.cases().size(), 7);
        QCOMPARE(store.selectedId(), QStringLiteral("TC-104"));
    }

    void loadsRepositoryContentWhenPresent() {
        auto repo = std::make_shared<MemoryTestCaseRepository>();
        TestCase only;
        only.id = QStringLiteral("TC-9");
        repo->cases = QList<TestCase>{only};
        TestCaseStore store(repo);
        store.load();
        QCOMPARE(store.cases().size(), 1);
        QCOMPARE(store.selectedId(), QStringLiteral("TC-9"));
    }

    void createCaseAssignsNextIdAndSaves() {
        AppFixture f;
        const QString id = f.store.createCase();
        QCOMPARE(id, QStringLiteral("TC-108"));
        QCOMPARE(f.store.selectedId(), id);
        QCOMPARE(f.store.find(id)->steps.size(), 1);   // nace con un paso vacío
        QVERIFY(f.store.save());
        QVERIFY(f.repo->caseSaves > 0);
    }

    void suitesComeFromCasesAndNewCaseInheritsSelectedSuite() {
        AppFixture f;
        QCOMPARE(f.store.suites(), (QStringList{QStringLiteral("Autenticación"), QStringLiteral("Checkout"), QStringLiteral("Notificaciones"), QStringLiteral("Perfil")}));
        f.store.updateCase(QStringLiteral("TC-106"), [](TestCase& c) { c.suite = QStringLiteral("Facturación"); });
        QVERIFY(f.store.suites().contains(QStringLiteral("Facturación")));
        QVERIFY(!f.store.suites().contains(QStringLiteral("Perfil")));   // ya nadie la usa
        f.store.select(QStringLiteral("TC-106"));
        QCOMPARE(f.store.find(f.store.createCase())->suite, QStringLiteral("Facturación"));
    }

    void duplicateCaseCopiesContentButNotResultsOrShots() {
        AppFixture f;
        f.store.addShot(QStringLiteral("TC-104"), Screenshot{1, 1, QStringLiteral("a.png"), {}});
        f.store.updateCase(QStringLiteral("TC-104"), [](TestCase& c) { c.testKey = QStringLiteral("SHOP-42"); });
        const QString id = f.store.duplicateCase(QStringLiteral("TC-104"));
        QCOMPARE(id, QStringLiteral("TC-108"));
        const TestCase* c = f.store.find(id);
        QVERIFY(c);
        QVERIFY(c->title.endsWith(QStringLiteral("(copia)")));
        QCOMPARE(c->steps.size(), 4);
        QCOMPARE(c->suite, QStringLiteral("Checkout"));
        QCOMPARE(static_cast<int>(c->status), static_cast<int>(CaseStatus::Borrador));
        QCOMPARE(static_cast<int>(c->lastRun.outcome), static_cast<int>(RunOutcome::None));
        QVERIFY(c->shots.isEmpty());
        // La copia nace sin Test de Zephyr: el del original es del original.
        QVERIFY(c->testKey.isEmpty());
        QCOMPARE(f.store.find(QStringLiteral("TC-104"))->testKey, QStringLiteral("SHOP-42"));
        QCOMPARE(f.store.cases()[4].id, id);   // justo después del original (índice 3)
        QCOMPARE(f.store.selectedId(), id);
    }

    void mergeCasesAddsAndUpdatesKeepingLocalData() {
        AppFixture f;
        f.store.addShot(QStringLiteral("TC-104"), Screenshot{1, 1, QStringLiteral("a.png"), {}});
        TestCase updated = *f.store.find(QStringLiteral("TC-104"));
        updated.title = QStringLiteral("Nuevo título");
        updated.shots.clear();
        updated.lastRun = LastRun{};
        TestCase fresh;
        fresh.id = QStringLiteral("TC-500");
        fresh.title = QStringLiteral("Importado");

        const auto [added, upd] = f.store.mergeCases({updated, fresh});
        QCOMPARE(added, 1);
        QCOMPARE(upd, 1);
        const TestCase* c = f.store.find(QStringLiteral("TC-104"));
        QCOMPARE(c->title, QStringLiteral("Nuevo título"));
        QCOMPARE(c->shots.size(), 1);                                                   // capturas locales conservadas
        QCOMPARE(static_cast<int>(c->lastRun.outcome), static_cast<int>(RunOutcome::Passed)); // y la última ejecución
        QVERIFY(f.store.find(QStringLiteral("TC-500")));
        QCOMPARE(f.store.nextCaseId(), QStringLiteral("TC-501"));
    }

    // ---- Pasos -------------------------------------------------------------------------

    void removingStepReassignsShots() {
        AppFixture f;
        const QString id = QStringLiteral("TC-104");   // 4 pasos
        f.store.addShot(id, Screenshot{1, 2, QStringLiteral("a.png"), {}});
        f.store.addShot(id, Screenshot{2, 4, QStringLiteral("b.png"), {}});
        f.store.removeStep(id, 1);   // borra el paso 2
        const TestCase* c = f.store.find(id);
        QCOMPARE(c->steps.size(), 3);
        QCOMPARE(c->shots[0].step, 0);   // quedó sin asignar
        QCOMPARE(c->shots[1].step, 3);   // se desplazó
    }

    void moveAndInsertStepsKeepShotAssignments() {
        AppFixture f;
        const QString id = QStringLiteral("TC-104");   // 4 pasos
        f.store.addShot(id, Screenshot{1, 1, QStringLiteral("a.png"), {}});
        f.store.addShot(id, Screenshot{2, 3, QStringLiteral("b.png"), {}});
        const QString first = f.store.find(id)->steps[0].action;

        f.store.moveStep(id, 0, +2);   // paso 1 → posición 3
        const TestCase* c = f.store.find(id);
        QCOMPARE(c->steps[2].action, first);
        QCOMPARE(c->shots[0].step, 3);   // sigue a su paso
        QCOMPARE(c->shots[1].step, 2);   // el antiguo paso 3 subió una posición

        f.store.insertStep(id, 1);     // paso vacío en la posición 2
        c = f.store.find(id);
        QCOMPARE(c->steps.size(), 5);
        QVERIFY(c->steps[1].action.isEmpty());
        QCOMPARE(c->shots[0].step, 4);
        QCOMPARE(c->shots[1].step, 3);

        f.store.moveStep(id, 0, -1);   // fuera de rango: sin efecto
        QCOMPARE(f.store.find(id)->steps.size(), 5);
    }

    // ---- Borrado y deshacer ------------------------------------------------------------

    void removeCaseSelectsNeighbourAndCanBeUndone() {
        AppFixture f;
        QSignalSpy undoSpy(&f.store, &TestCaseStore::undoAvailable);
        QSignalSpy filesSpy(&f.store, &TestCaseStore::filesReleased);
        f.store.addShot(QStringLiteral("TC-104"), Screenshot{1, 1, QStringLiteral("a.png"), QStringLiteral("/tmp/a.png")});
        f.store.select(QStringLiteral("TC-104"));

        f.store.removeCase(QStringLiteral("TC-104"));
        QCOMPARE(f.store.cases().size(), 6);
        QVERIFY(!f.store.find(QStringLiteral("TC-104")));
        QCOMPARE(f.store.selectedId(), QStringLiteral("TC-105"));   // el siguiente en la lista
        QCOMPARE(undoSpy.count(), 1);
        QVERIFY(f.store.canUndo());
        QCOMPARE(filesSpy.count(), 0);   // el fichero espera por si se deshace

        QVERIFY(f.store.undo());
        QCOMPARE(f.store.cases().size(), 7);
        QCOMPARE(f.store.selectedId(), QStringLiteral("TC-104"));
        QCOMPARE(f.store.find(QStringLiteral("TC-104"))->shots.size(), 1);
        QVERIFY(!f.store.canUndo());
        QCOMPARE(filesSpy.count(), 0);
    }

    void removeStepIsUndoable() {
        AppFixture f;
        f.store.removeStep(QStringLiteral("TC-104"), 0);
        QCOMPARE(f.store.find(QStringLiteral("TC-104"))->steps.size(), 3);
        QVERIFY(f.store.undo());
        QCOMPARE(f.store.find(QStringLiteral("TC-104"))->steps.size(), 4);
        QVERIFY(!f.store.undo());   // sólo un nivel
    }

    void anyOtherEditCommitsUndoAndReleasesFiles() {
        AppFixture f;
        QSignalSpy filesSpy(&f.store, &TestCaseStore::filesReleased);
        f.store.addShot(QStringLiteral("TC-104"), Screenshot{1, 1, QStringLiteral("a.png"), QStringLiteral("/tmp/a.png")});
        f.store.removeShot(QStringLiteral("TC-104"), 1);
        QVERIFY(f.store.canUndo());

        f.store.updateCase(QStringLiteral("TC-101"), [](TestCase& c) { c.title = QStringLiteral("x"); });
        QVERIFY(!f.store.canUndo());
        QCOMPARE(filesSpy.count(), 1);
        QCOMPARE(filesSpy.first().first().toStringList(), QStringList{QStringLiteral("/tmp/a.png")});
    }
};

QTEST_APPLESS_MAIN(TestCaseStoreTest)
#include "test_test_case_store.moc"
