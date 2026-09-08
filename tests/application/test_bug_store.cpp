// BugStore (application/BugStore.h): issues enlazados y cola de pendientes.

#include "support/MemoryRepositories.h"

#include "application/BugStore.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::MemoryBugRepository;

namespace {
IssueLink link(const QString& key, const QString& caseId) {
    IssueLink l;
    l.key = key; l.caseId = caseId; l.tracker = QStringLiteral("Jira"); l.title = QStringLiteral("Bug ") + key;
    l.createdAt = QDateTime::currentDateTime();
    return l;
}
} // namespace

class BugStoreTest : public QObject {
    Q_OBJECT
private slots:
    void recordsIssuesPerCaseNewestFirst() {
        auto repo = std::make_shared<MemoryBugRepository>();
        BugStore store(repo);
        store.load();
        QSignalSpy changed(&store, &BugStore::bugsChanged);
        store.recordIssue(link(QStringLiteral("SHOP-1"), QStringLiteral("TC-104")));
        store.recordIssue(link(QStringLiteral("SHOP-2"), QStringLiteral("TC-104")));
        store.recordIssue(link(QStringLiteral("SHOP-3"), QStringLiteral("TC-101")));
        QCOMPARE(store.issues().size(), 3);
        QCOMPARE(store.openIssueCount(), 3);
        const auto forCase = store.issuesForCase(QStringLiteral("TC-104"));
        QCOMPARE(forCase.size(), 2);
        QCOMPARE(forCase.first().key, QStringLiteral("SHOP-2"));
        QCOMPARE(changed.count(), 3);
        QCOMPARE(repo->saves, 3);
    }

    void recordingSameKeyReplacesIt() {
        BugStore store(std::make_shared<MemoryBugRepository>());
        store.load();
        store.recordIssue(link(QStringLiteral("SHOP-1"), QStringLiteral("TC-104")));
        IssueLink again = link(QStringLiteral("SHOP-1"), QStringLiteral("TC-104"));
        again.title = QStringLiteral("otro título");
        store.recordIssue(again);
        QCOMPARE(store.issues().size(), 1);
        QCOMPARE(store.findIssue(QStringLiteral("SHOP-1"))->title, QStringLiteral("otro título"));
    }

    void updateStatusMarksResolvedAndTimestamp() {
        BugStore store(std::make_shared<MemoryBugRepository>());
        store.load();
        store.recordIssue(link(QStringLiteral("SHOP-1"), QStringLiteral("TC-104")));
        store.updateStatus(QStringLiteral("SHOP-1"), QStringLiteral("Done"), true);
        const IssueLink* i = store.findIssue(QStringLiteral("SHOP-1"));
        QCOMPARE(i->status, QStringLiteral("Done"));
        QVERIFY(i->resolved);
        QVERIFY(i->statusCheckedAt.isValid());
        QCOMPARE(store.openIssueCount(), 0);
        store.updateStatus(QStringLiteral("SHOP-9"), QStringLiteral("x"), false);   // desconocido: se ignora
        store.forgetIssue(QStringLiteral("SHOP-1"));
        QVERIFY(store.issues().isEmpty());
    }

    void pendingQueueAssignsIdsAndCountsAttempts() {
        BugStore store(std::make_shared<MemoryBugRepository>());
        store.load();
        BugReport b;
        b.title = QStringLiteral("t"); b.actual = QStringLiteral("a");
        const QString id = store.enqueue(b, QStringLiteral("Host not found"));
        QCOMPARE(id, QStringLiteral("Q-0001"));
        QCOMPARE(store.enqueue(b, QString()), QStringLiteral("Q-0002"));
        QCOMPARE(store.findPending(id)->attempts, 1);
        store.markAttempt(id, QStringLiteral("Timeout"));
        QCOMPARE(store.findPending(id)->attempts, 2);
        QCOMPARE(store.findPending(id)->lastError, QStringLiteral("Timeout"));
        store.removePending(id);
        QCOMPARE(store.pending().size(), 1);
        QVERIFY(!store.findPending(id));
    }

    void ledgerRoundTripsThroughRepository() {
        auto repo = std::make_shared<MemoryBugRepository>();
        {
            BugStore store(repo);
            store.load();
            store.recordIssue(link(QStringLiteral("SHOP-1"), QStringLiteral("TC-104")));
            BugReport b;
            b.title = QStringLiteral("pendiente"); b.actual = QStringLiteral("a");
            store.enqueue(b, QStringLiteral("sin red"));
        }
        BugStore again(repo);
        again.load();
        QCOMPARE(again.issues().size(), 1);
        QCOMPARE(again.pending().size(), 1);
        QCOMPARE(again.pending().first().report.title, QStringLiteral("pendiente"));
    }
};

QTEST_APPLESS_MAIN(BugStoreTest)
#include "test_bug_store.moc"
