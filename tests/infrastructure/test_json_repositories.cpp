// Repositorios JSON (infrastructure/persistence/): ida y vuelta por disco en un directorio
// temporal, ficheros ausentes o corruptos, migración de plan.json y fallos de escritura.

#include "infrastructure/persistence/JsonBugRepository.h"
#include "infrastructure/persistence/JsonRunHistoryRepository.h"
#include "infrastructure/persistence/JsonRunSessionRepository.h"
#include "infrastructure/persistence/JsonTestCaseRepository.h"

#include <QTemporaryDir>
#include <QtTest>

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

using namespace qaflow;

namespace {
TestCase sampleCase() {
    TestCase c;
    c.id = QStringLiteral("TC-104"); c.title = QStringLiteral("Pago con cupón"); c.suite = QStringLiteral("Checkout");
    c.priority = Priority::Alta; c.status = CaseStatus::Listo; c.preconditions = QStringLiteral("Carrito con 2 productos");
    c.steps = {TestStep{QStringLiteral("Ir al carrito"), QStringLiteral("Se ve el resumen")}, TestStep{QStringLiteral("Aplicar cupón"), QStringLiteral("Baja el total")}};
    c.shots = {Screenshot{4, 2, QStringLiteral("cap_004.png"), QStringLiteral("/tmp/cap_004.png")}};
    c.tags = {QStringLiteral("regresión"), QStringLiteral("pagos")};
    c.component = QStringLiteral("Carrito"); c.jiraKey = QStringLiteral("SHOP-12");
    c.lastRun = LastRun{RunOutcome::Failed, QDateTime(QDate(2026, 3, 1), QTime(10, 30))};
    return c;
}
void writeFile(const QString& path, const QByteArray& content) {
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(content);
}
} // namespace

class JsonRepositoriesTest : public QObject {
    Q_OBJECT
private slots:
    void missingFilesReturnNullopt() {
        QTemporaryDir dir;
        JsonTestCaseRepository cases(dir.path());
        QVERIFY(!cases.loadCases());
        QVERIFY(!cases.loadPlans());
        QVERIFY(!JsonRunHistoryRepository(dir.path()).loadHistory());
        QVERIFY(!JsonRunSessionRepository(dir.path()).loadSession());
        QVERIFY(!JsonBugRepository(dir.path()).loadLedger());
    }

    void casesRoundTripThroughDisk() {
        QTemporaryDir dir;
        JsonTestCaseRepository repo(dir.path());
        QVERIFY(repo.saveCases({sampleCase()}));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("cases.json"))));
        const auto loaded = repo.loadCases();
        QVERIFY(loaded);
        QCOMPARE(loaded->size(), 1);
        const TestCase& c = loaded->first();
        QCOMPARE(c.id, QStringLiteral("TC-104"));
        QCOMPARE(c.title, QStringLiteral("Pago con cupón"));
        QCOMPARE(static_cast<int>(c.priority), static_cast<int>(Priority::Alta));
        QCOMPARE(static_cast<int>(c.status), static_cast<int>(CaseStatus::Listo));
        QCOMPARE(c.steps.size(), 2);
        QCOMPARE(c.steps[1].expected, QStringLiteral("Baja el total"));
        QCOMPARE(c.shots.size(), 1);
        QCOMPARE(c.shots[0].step, 2);
        QCOMPARE(c.tags, (QStringList{QStringLiteral("regresión"), QStringLiteral("pagos")}));
        QCOMPARE(c.component, QStringLiteral("Carrito"));
        QCOMPARE(c.jiraKey, QStringLiteral("SHOP-12"));
        QCOMPARE(static_cast<int>(c.lastRun.outcome), static_cast<int>(RunOutcome::Failed));
        QCOMPARE(c.lastRun.at, QDateTime(QDate(2026, 3, 1), QTime(10, 30)));
    }

    void corruptCasesFileIsIgnored() {
        QTemporaryDir dir;
        writeFile(dir.filePath(QStringLiteral("cases.json")), "{ esto no es json");
        QVERIFY(!JsonTestCaseRepository(dir.path()).loadCases());
        writeFile(dir.filePath(QStringLiteral("cases.json")), "{\"no\": \"es un array\"}");
        QVERIFY(!JsonTestCaseRepository(dir.path()).loadCases());
    }

    void plansRoundTripAndLegacyPlanIsMigrated() {
        QTemporaryDir dir;
        JsonTestCaseRepository repo(dir.path());
        TestPlan p;
        p.id = QStringLiteral("PL-0002"); p.name = QStringLiteral("Smoke"); p.caseIds = {QStringLiteral("TC-104"), QStringLiteral("TC-101")};
        p.archived = true; p.createdAt = QDateTime(QDate(2026, 2, 1), QTime(9, 0));
        QVERIFY(repo.savePlans(PlanCollection{p.id, {p}}));
        const auto col = repo.loadPlans();
        QVERIFY(col);
        QCOMPARE(col->activeId, QStringLiteral("PL-0002"));
        QCOMPARE(col->plans.size(), 1);
        QCOMPARE(col->plans[0].caseIds, (QStringList{QStringLiteral("TC-104"), QStringLiteral("TC-101")}));
        QVERIFY(col->plans[0].archived);
        QCOMPARE(col->plans[0].createdAt, p.createdAt);

        // Versión anterior: un único plan en plan.json y sin plans.json
        QTemporaryDir legacyDir;
        writeFile(legacyDir.filePath(QStringLiteral("plan.json")), R"({"name":"Regresión","caseIds":["TC-101"]})");
        const auto migrated = JsonTestCaseRepository(legacyDir.path()).loadPlans();
        QVERIFY(migrated);
        QCOMPARE(migrated->activeId, QStringLiteral("PL-0001"));
        QCOMPARE(migrated->plans[0].name, QStringLiteral("Regresión"));
        QCOMPARE(migrated->plans[0].caseIds, QStringList{QStringLiteral("TC-101")});
    }

    void historyRoundTripKeepsRunsAndPlans() {
        QTemporaryDir dir;
        JsonRunHistoryRepository repo(dir.path());
        RunHistory h;
        RunRecord r;
        r.id = QStringLiteral("R-0001"); r.caseId = QStringLiteral("TC-104"); r.caseTitle = QStringLiteral("Pago"); r.suite = QStringLiteral("Checkout");
        r.planRunId = QStringLiteral("PR-0001"); r.startedAt = QDateTime(QDate(2026, 3, 1), QTime(10, 0)); r.finishedAt = r.startedAt.addSecs(90);
        r.verdict = Verdict::Bloqueado; r.plannedSteps = 3; r.durationSecs = 90;
        r.steps = {RunRecordStep{QStringLiteral("a"), QStringLiteral("e"), StepResult::Pass, QString(), 30}, RunRecordStep{QStringLiteral("b"), QStringLiteral("f"), StepResult::Block, QStringLiteral("caído"), 60}};
        h.runs << r;
        PlanRun p;
        p.id = QStringLiteral("PR-0001"); p.planId = QStringLiteral("PL-0001"); p.name = QStringLiteral("Regresión"); p.caseIds = {QStringLiteral("TC-104")};
        p.startedAt = r.startedAt; p.finishedAt = r.finishedAt;
        p.zephyrCycleId = QStringLiteral("77"); p.publishedAt = r.finishedAt.addSecs(600);
        h.plans << p;
        QVERIFY(repo.saveHistory(h));
        const auto loaded = repo.loadHistory();
        QVERIFY(loaded);
        QCOMPARE(loaded->runs.size(), 1);
        QCOMPARE(static_cast<int>(loaded->runs[0].verdict), static_cast<int>(Verdict::Bloqueado));
        QCOMPARE(loaded->runs[0].steps.size(), 2);
        QCOMPARE(static_cast<int>(loaded->runs[0].steps[1].result), static_cast<int>(StepResult::Block));
        QCOMPARE(loaded->runs[0].steps[1].note, QStringLiteral("caído"));
        QCOMPARE(loaded->runs[0].steps[1].durationSecs, 60);
        QCOMPARE(loaded->runs[0].durationSecs, 90);
        QCOMPARE(loaded->plans.size(), 1);
        QCOMPARE(loaded->plans[0].planId, QStringLiteral("PL-0001"));
        QVERIFY(loaded->plans[0].isFinished());
        // Dónde se publicaron esos resultados sobrevive al cierre de la aplicación.
        QVERIFY(loaded->plans[0].isPublished());
        QCOMPARE(loaded->plans[0].zephyrCycleId, QStringLiteral("77"));
        QCOMPARE(loaded->plans[0].publishedAt, p.publishedAt);
    }

    void sessionIsSavedAndCleared() {
        QTemporaryDir dir;
        JsonRunSessionRepository repo(dir.path());
        RunSession s;
        s.run.caseId = QStringLiteral("TC-104"); s.run.idx = 1; s.run.note = QStringLiteral("nota");
        s.run.results = {StepRecord{StepResult::Fail, QStringLiteral("no descuenta"), 12}};
        s.run.startedAt = QDateTime(QDate(2026, 3, 1), QTime(10, 0)); s.run.stepElapsedSecs = 7;
        s.queue = {QStringLiteral("TC-105")}; s.planRunId = QStringLiteral("PR-0003");
        QVERIFY(repo.saveSession(s));
        const auto loaded = repo.loadSession();
        QVERIFY(loaded);
        QCOMPARE(loaded->run.caseId, QStringLiteral("TC-104"));
        QCOMPARE(loaded->run.idx, 1);
        QCOMPARE(loaded->run.results.size(), 1);
        QCOMPARE(static_cast<int>(loaded->run.results[0].result), static_cast<int>(StepResult::Fail));
        QCOMPARE(loaded->run.results[0].durationSecs, 12);
        QCOMPARE(loaded->run.stepElapsedSecs, 7);
        QCOMPARE(loaded->queue, QStringList{QStringLiteral("TC-105")});
        QCOMPARE(loaded->planRunId, QStringLiteral("PR-0003"));
        repo.clearSession();
        QVERIFY(!repo.loadSession());
        QVERIFY(!QFile::exists(dir.filePath(QStringLiteral("session.json"))));
    }

    void bugLedgerRoundTrip() {
        QTemporaryDir dir;
        JsonBugRepository repo(dir.path());
        BugLedger ledger;
        IssueLink l;
        l.key = QStringLiteral("SHOP-143"); l.url = QStringLiteral("https://acme/browse/SHOP-143"); l.title = QStringLiteral("Cupón");
        l.caseId = QStringLiteral("TC-104"); l.tracker = QStringLiteral("Jira"); l.severity = QStringLiteral("Mayor");
        l.status = QStringLiteral("Done"); l.resolved = true; l.createdAt = QDateTime(QDate(2026, 3, 1), QTime(10, 0)); l.statusCheckedAt = l.createdAt.addDays(1);
        ledger.issues << l;
        PendingBug p;
        p.id = QStringLiteral("Q-0001"); p.report.title = QStringLiteral("Pendiente"); p.report.actual = QStringLiteral("x");
        p.report.attachmentPaths = {QStringLiteral("/tmp/a.png")}; p.createdAt = l.createdAt; p.lastError = QStringLiteral("Host not found"); p.attempts = 2;
        ledger.pending << p;
        QVERIFY(repo.saveLedger(ledger));
        const auto loaded = repo.loadLedger();
        QVERIFY(loaded);
        QCOMPARE(loaded->issues.size(), 1);
        QCOMPARE(loaded->issues[0].key, QStringLiteral("SHOP-143"));
        QVERIFY(loaded->issues[0].resolved);
        QCOMPARE(loaded->issues[0].statusCheckedAt, l.statusCheckedAt);
        QCOMPARE(loaded->pending.size(), 1);
        QCOMPARE(loaded->pending[0].id, QStringLiteral("Q-0001"));
        QCOMPARE(loaded->pending[0].attempts, 2);
        QCOMPARE(loaded->pending[0].report.attachmentPaths, QStringList{QStringLiteral("/tmp/a.png")});
    }

    void saveFailsWhenDirectoryIsNotWritable() {
#ifdef Q_OS_WIN
        QSKIP("en NTFS quitar el permiso de escritura al directorio no impide crear ficheros dentro");
#endif
#ifdef Q_OS_UNIX
        if (geteuid() == 0) QSKIP("root puede escribir en cualquier sitio");
#endif
        QTemporaryDir dir;
        const QString readOnly = dir.filePath(QStringLiteral("ro"));
        QVERIFY(QDir().mkpath(readOnly));
        QVERIFY(QFile::setPermissions(readOnly, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        JsonTestCaseRepository repo(readOnly);
        QVERIFY(!repo.saveCases({sampleCase()}));
        QVERIFY(!JsonRunHistoryRepository(readOnly).saveHistory(RunHistory{}));
        QVERIFY(!JsonBugRepository(readOnly).saveLedger(BugLedger{}));
        QFile::setPermissions(readOnly, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
};

QTEST_APPLESS_MAIN(JsonRepositoriesTest)
#include "test_json_repositories.moc"
