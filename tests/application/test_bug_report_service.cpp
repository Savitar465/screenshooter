// BugReportService (application/BugReportService.h): borrador, envío, cola offline,
// estados y metadatos, sobre un gestor falso.

#include "support/AppFixture.h"
#include "support/FakeIssueTracker.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::AppFixture;
using qaflow::testing::FakeIssueTracker;

class BugReportServiceTest : public QObject {
    Q_OBJECT
private slots:
    void draftUsesFailedStepAndCaseMetadata() {
        AppFixture f;
        f.store.select(QStringLiteral("TC-104"));   // componente Carrito, historia SHOP-12
        f.run.start(QStringLiteral("TC-104"));
        f.run.mark(StepResult::Pass);
        f.run.setNote(QStringLiteral("no descuenta"));
        f.run.mark(StepResult::Fail);

        const BugReport d = f.bugs.draftFromCurrentContext();
        QVERIFY(d.title.contains(QStringLiteral("Falla en paso 2")));
        QCOMPARE(d.actual, QStringLiteral("no descuenta"));
        QCOMPARE(d.linkedCaseId, QStringLiteral("TC-104"));
        QCOMPARE(d.linkedStoryKey, QStringLiteral("SHOP-12"));
        QCOMPARE(d.components, QStringList{QStringLiteral("Carrito")});
        QCOMPARE(d.priority, QStringLiteral("Medium"));   // severidad Mayor por defecto
        QVERIFY(d.stepsToReproduce.startsWith(QStringLiteral("1. ")));
    }

    void successfulSubmitRecordsLinkedIssue() {
        AppFixture f;
        BugReport b = f.bugs.draftFromCurrentContext();
        b.title = QStringLiteral("Falla el cupón"); b.actual = QStringLiteral("no descuenta");
        BugReportService::SubmitResult out;
        f.bugs.submit(b, [&](const BugReportService::SubmitResult& r) { out = r; });
        QVERIFY(out.ok);
        QCOMPARE(out.key, QStringLiteral("SHOP-100"));
        QCOMPARE(out.url, QStringLiteral("https://acme.atlassian.net/browse/SHOP-100"));
        QCOMPARE(f.bugLedger.issues().size(), 1);
        const IssueLink& l = f.bugLedger.issues().first();
        QCOMPARE(l.caseId, QStringLiteral("TC-104"));
        QCOMPARE(l.tracker, QStringLiteral("Jira"));
        QCOMPARE(l.title, QStringLiteral("Falla el cupón"));
        QCOMPARE(f.bugLedger.issuesForCase(QStringLiteral("TC-104")).size(), 1);
        QVERIFY(f.bugLedger.pending().isEmpty());
    }

    void networkFailureQueuesTheBug() {
        AppFixture f;
        f.tracker->mode = FakeIssueTracker::Mode::NetworkDown;
        BugReport b;
        b.title = QStringLiteral("t"); b.actual = QStringLiteral("a"); b.linkedCaseId = QStringLiteral("TC-101");
        BugReportService::SubmitResult out;
        f.bugs.submit(b, [&](const BugReportService::SubmitResult& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(out.queued);
        QCOMPARE(f.bugLedger.pending().size(), 1);
        QCOMPARE(f.bugLedger.pending().first().lastError, QStringLiteral("Host not found"));
        QVERIFY(f.bugLedger.issues().isEmpty());
    }

    void contentRejectionIsNotQueued() {
        AppFixture f;
        f.tracker->mode = FakeIssueTracker::Mode::RejectContent;
        BugReport b;
        b.title = QStringLiteral("t"); b.actual = QStringLiteral("a");
        BugReportService::SubmitResult out;
        f.bugs.submit(b, [&](const BugReportService::SubmitResult& r) { out = r; });
        QVERIFY(!out.ok);
        QVERIFY(!out.queued);
        QVERIFY(out.error.contains(QStringLiteral("HTTP 400")));
        QVERIFY(f.bugLedger.pending().isEmpty());
    }

    void retryPendingSendsInOrderAndStopsWhenOffline() {
        AppFixture f;
        f.tracker->mode = FakeIssueTracker::Mode::NetworkDown;
        for (const auto& t : {"uno", "dos", "tres"}) {
            BugReport b;
            b.title = QLatin1String(t); b.actual = QStringLiteral("a");
            f.bugs.submit(b, [](const BugReportService::SubmitResult&) {});
        }
        QCOMPARE(f.bugLedger.pending().size(), 3);

        // Sigue sin red: nada se envía y se detiene al primer fallo.
        BugReportService::RetryResult r;
        f.bugs.retryPending([&](const BugReportService::RetryResult& x) { r = x; });
        QCOMPARE(r.sent, 0);
        QCOMPARE(r.failed, 3);
        QCOMPARE(f.bugLedger.pending().first().attempts, 2);

        // Vuelve la red: se envían en orden y la cola se vacía.
        f.tracker->mode = FakeIssueTracker::Mode::Succeed;
        f.bugs.retryPending([&](const BugReportService::RetryResult& x) { r = x; });
        QCOMPARE(r.sent, 3);
        QCOMPARE(r.failed, 0);
        QCOMPARE(r.keys, (QStringList{QStringLiteral("SHOP-100"), QStringLiteral("SHOP-101"), QStringLiteral("SHOP-102")}));
        QVERIFY(f.bugLedger.pending().isEmpty());
        QCOMPARE(f.bugLedger.issues().size(), 3);
        QCOMPARE(f.bugLedger.issues()[0].title, QStringLiteral("uno"));
    }

    void retrySkipsRejectedContentAndContinues() {
        AppFixture f;
        f.tracker->mode = FakeIssueTracker::Mode::NetworkDown;
        BugReport b;
        b.title = QStringLiteral("t"); b.actual = QStringLiteral("a");
        f.bugs.submit(b, [](const BugReportService::SubmitResult&) {});
        f.bugs.submit(b, [](const BugReportService::SubmitResult&) {});
        f.tracker->mode = FakeIssueTracker::Mode::RejectContent;
        BugReportService::RetryResult r;
        f.bugs.retryPending([&](const BugReportService::RetryResult& x) { r = x; });
        QCOMPARE(r.sent, 0);
        QCOMPARE(r.failed, 2);                    // los dos se intentaron
        QCOMPARE(f.bugLedger.pending().size(), 2); // y siguen en la cola con su error
        QVERIFY(f.bugLedger.pending().first().lastError.contains(QStringLiteral("HTTP 400")));
        f.bugs.discardPending(f.bugLedger.pending().first().id);
        QCOMPARE(f.bugLedger.pending().size(), 1);
    }

    void refreshStatusesQueriesOnlyCurrentTrackerIssues() {
        AppFixture f;
        BugReport b;
        b.title = QStringLiteral("t"); b.actual = QStringLiteral("a");
        f.bugs.submit(b, [](const BugReportService::SubmitResult&) {});
        IssueLink other;
        other.key = QStringLiteral("#3"); other.tracker = QStringLiteral("GitHub");
        f.bugLedger.recordIssue(other);

        f.tracker->statusToReturn = QStringLiteral("Done");
        BugReportService::RefreshResult r;
        f.bugs.refreshStatuses(false, [&](const BugReportService::RefreshResult& x) { r = x; });
        QCOMPARE(r.updated, 1);
        QCOMPARE(f.tracker->statusQueries, QStringList{QStringLiteral("SHOP-100")});
        QVERIFY(f.bugLedger.findIssue(QStringLiteral("SHOP-100"))->resolved);
        QCOMPARE(f.bugLedger.openIssueCount(), 1);   // el de GitHub sigue "abierto"

        f.bugs.refreshStatuses(true, [&](const BugReportService::RefreshResult& x) { r = x; });   // sólo abiertos: ninguno de Jira
        QCOMPARE(r.updated, 0);
    }

    void metadataIsCachedPerProjectAndInvalidatedOnChange() {
        AppFixture f;
        f.tracker->metadataToReturn.issueTypes = {QStringLiteral("Bug"), QStringLiteral("Task")};
        f.tracker->metadataToReturn.assignees = {Assignee{QStringLiteral("abc"), QStringLiteral("Ana")}};
        QSignalSpy changed(&f.bugs, &BugReportService::metadataChanged);
        MetadataResult r;
        f.bugs.loadMetadata(false, [&](const MetadataResult& x) { r = x; });
        QVERIFY(r.ok);
        QVERIFY(f.bugs.hasMetadata());
        QCOMPARE(f.bugs.metadata().issueTypes.size(), 2);
        QCOMPARE(changed.count(), 1);
        f.bugs.loadMetadata(false, [&](const MetadataResult& x) { r = x; });
        QCOMPARE(f.tracker->metadataCalls, 1);    // caché
        f.bugs.loadMetadata(true, [&](const MetadataResult& x) { r = x; });
        QCOMPARE(f.tracker->metadataCalls, 2);    // forzado
        f.settings.updateTracker([](TrackerSettings& s) { s.project = QStringLiteral("OTRO"); });
        QVERIFY(!f.bugs.hasMetadata());           // otro proyecto: caché invalidada
    }
};

QTEST_APPLESS_MAIN(BugReportServiceTest)
#include "test_bug_report_service.moc"
