// IssueDirectory (application/IssueDirectory.h): los issues de todos los proyectos para la vista general,
// dónde está el issue de un requerimiento y la bandeja de GESREQ puesta al día en todos los proyectos,
// también en los que no tienen la sesión abierta.

#include "support/MemoryRepositories.h"

#include "application/IssueDirectory.h"
#include "application/IssueStore.h"
#include "application/ProjectStore.h"

#include <QSignalSpy>
#include <QtTest>

using namespace qaflow;
using qaflow::testing::MemoryIssueRepository;
using qaflow::testing::MemoryProjectRepository;

namespace {
const QString kConnection = QStringLiteral("http://gesreq.test:7401/greq");

ExternalRequirement requirementOf(const QString& id, const QString& code, const QString& state = QStringLiteral("CONTROL CALIDAD ASIGNADO")) {
    ExternalRequirement r;
    r.id = id;
    r.systemCode = code;
    r.system = code + QStringLiteral("-SISTEMA");
    r.summary = QStringLiteral("Requerimiento %1").arg(id);
    r.states = {state};
    return r;
}

/// Dos proyectos: el principal, con su sesión (y su store) abierta, y «Riesgos», sin abrir, con sus issues
/// en su repositorio.
struct Fixture {
    ProjectStore projects{std::make_shared<MemoryProjectRepository>()};
    QString mainId;
    QString otherId;
    std::shared_ptr<MemoryIssueRepository> mainRepo = std::make_shared<MemoryIssueRepository>();
    std::shared_ptr<MemoryIssueRepository> otherRepo = std::make_shared<MemoryIssueRepository>();
    int repositoryReads = 0;
    IssueStore mainStore{mainRepo};
    std::unique_ptr<IssueDirectory> directory;

    Fixture() {
        projects.load();
        mainId = projects.activeId();
        otherId = projects.create(QStringLiteral("Riesgos"));
        // El proyecto sin abrir ya tiene un requerimiento importado de la bandeja.
        IssueStore seed(otherRepo);
        seed.load();
        seed.openForRequirement(requirementOf(QStringLiteral("2025719"), QStringLiteral("SEGRAN")), kConnection);
        seed.createIssue(QStringLiteral("Hecho a mano"));
        mainStore.load();
        directory = std::make_unique<IssueDirectory>(projects, [this](const QString& id) -> std::shared_ptr<IIssueRepository> {
            ++repositoryReads;
            if (id == mainId) return mainRepo;
            if (id == otherId) return otherRepo;
            return nullptr;
        });
        directory->attach(mainId, &mainStore);
    }
};
} // namespace

class IssueDirectoryTest : public QObject {
    Q_OBJECT
private slots:
    // La vista general reúne los issues de todos los proyectos, cada uno con el suyo; los del proyecto
    // abierto se leen de su store, así que lo que cambia allí se ve al momento.
    void listsTheIssuesOfEveryProjectWithTheirProject() {
        Fixture f;
        f.mainStore.openForRequirement(requirementOf(QStringLiteral("2025175"), QStringLiteral("SUMA TRANSITO")), kConnection);

        const QList<IssueDirectory::Entry> all = f.directory->issues();
        QCOMPARE(all.size(), 3);
        QCOMPARE(all.first().projectId, f.mainId);
        QCOMPARE(all.at(1).projectId, f.otherId);
        QCOMPARE(all.at(1).issue.requirement.data.id, QStringLiteral("2025719"));

        const QList<IssueDirectory::Entry> others = f.directory->issues(f.mainId);
        QCOMPARE(others.size(), 2);
        for (const auto& entry : others) QCOMPARE(entry.projectId, f.otherId);

        QSignalSpy changed(f.directory.get(), &IssueDirectory::changed);
        f.mainStore.createIssue(QStringLiteral("Otro"));
        QVERIFY(changed.count() > 0);
        QCOMPARE(f.directory->issues().size(), 4);
    }

    // Lo leído del disco de un proyecto sin abrir se guarda: el tablero se redibuja a menudo.
    void readsTheIssuesOfAClosedProjectOnce() {
        Fixture f;
        f.directory->issues();
        f.directory->issues();
        QCOMPARE(f.repositoryReads, 1);
    }

    // Dónde está el issue de un requerimiento, en el proyecto que sea: con o sin barra final en la dirección.
    void findsTheIssueOfARequirementInAnyProject() {
        Fixture f;
        const auto found = f.directory->findByRequirement(kConnection + QStringLiteral("/"), QStringLiteral("2025719"));
        QVERIFY(found.has_value());
        QCOMPARE(found->projectId, f.otherId);
        QVERIFY(!f.directory->findByRequirement(kConnection, QStringLiteral("2025175")).has_value());
        QVERIFY(!f.directory->findByRequirement(QStringLiteral("http://otro-gesreq"), QStringLiteral("2025719")).has_value());
    }

    // Leer la bandeja pone al día los issues importados de todos los proyectos —también los del que no
    // está abierto, que se escriben en su repositorio—, sin crear ninguno.
    void theInboxUpdatesTheImportedIssuesOfEveryProject() {
        Fixture f;
        f.mainStore.openForRequirement(requirementOf(QStringLiteral("2025175"), QStringLiteral("SUMA TRANSITO")), kConnection);

        // 2025719 cambió de estado; 2025175 ya no está en la bandeja; 2026001 es nuevo y no tiene issue.
        f.directory->applyInbox({requirementOf(QStringLiteral("2025719"), QStringLiteral("SEGRAN"), QStringLiteral("CONTROL DE CALIDAD OBSERVADO")),
                                 requirementOf(QStringLiteral("2026001"), QStringLiteral("SEGRAN"))},
                                kConnection);

        QVERIFY(f.mainStore.findByRequirement(kConnection, QStringLiteral("2025175"))->requirement.missing);
        QCOMPARE(f.mainStore.issues().size(), 1);

        QCOMPARE(f.otherRepo->issues->size(), 2);   // no se creó el de 2026001
        const Issue& other = f.otherRepo->issues->first();
        QCOMPARE(other.requirement.data.states, QStringList{QStringLiteral("CONTROL DE CALIDAD OBSERVADO")});
        QCOMPARE(other.requirement.changes.size(), 1);
        QVERIFY(!other.requirement.missing);
        // Y la vista general enseña lo escrito, sin volver a leer el disco.
        const auto found = f.directory->findByRequirement(kConnection, QStringLiteral("2025719"));
        QVERIFY(found && !found->issue.requirement.changes.isEmpty());
    }

    // Unos issues que no se pueden leer no se escriben encima al poner al día la bandeja.
    void anUnreadableProjectIsNotOverwritten() {
        Fixture f;
        f.otherRepo->issues = std::nullopt;
        const int saves = f.otherRepo->saves;
        f.directory->applyInbox({}, kConnection);
        QCOMPARE(f.otherRepo->saves, saves);
        QVERIFY(f.directory->issues(f.mainId).isEmpty());
    }
};

QTEST_MAIN(IssueDirectoryTest)
#include "test_issue_directory.moc"
