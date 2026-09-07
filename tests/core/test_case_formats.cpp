// formats:: (core/models/CaseFormats.h): JSON, CSV y Markdown de casos de prueba.

#include "core/models/CaseFormats.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace qaflow;

namespace {
TestCase richCase() {
    TestCase c;
    c.id = QStringLiteral("TC-1"); c.title = QStringLiteral("Título, con coma"); c.suite = QStringLiteral("S");
    c.priority = Priority::Alta; c.status = CaseStatus::Listo;
    c.tags = {QStringLiteral("smoke"), QStringLiteral("api")}; c.component = QStringLiteral("Comp"); c.jiraKey = QStringLiteral("SHOP-9");
    c.preconditions = QStringLiteral("línea 1\nlínea 2");
    c.steps = {TestStep{QStringLiteral("Pulsar \"OK\""), QStringLiteral("Cierra")}, TestStep{QStringLiteral("Otro"), QStringLiteral("Más")}};
    c.shots = {Screenshot{3, 1, QStringLiteral("cap.png"), QStringLiteral("/x/cap.png")}};
    c.lastRun = LastRun{RunOutcome::Blocked, QDateTime(QDate(2026, 9, 7), QTime(10, 0))};
    return c;
}
} // namespace

class CaseFormatsTest : public QObject {
    Q_OBJECT
private slots:
    // ---- JSON --------------------------------------------------------------------------

    void jsonRoundTripKeepsEverything() {
        const TestCase c = richCase();
        const auto back = formats::casesFromJson(QJsonDocument(formats::casesToJson({c})).toJson());
        QVERIFY(back.has_value());
        QCOMPARE(back->size(), 1);
        const TestCase& r = back->first();
        QCOMPARE(r.title, c.title);
        QCOMPARE(r.tags, c.tags);
        QCOMPARE(r.component, c.component);
        QCOMPARE(r.jiraKey, c.jiraKey);
        QCOMPARE(r.preconditions, c.preconditions);
        QCOMPARE(r.steps.size(), 2);
        QCOMPARE(r.shots.size(), 1);
        QCOMPARE(static_cast<int>(r.priority), static_cast<int>(Priority::Alta));
        QCOMPARE(static_cast<int>(r.lastRun.outcome), static_cast<int>(RunOutcome::Blocked));
        QCOMPARE(r.lastRun.at, c.lastRun.at);
    }

    void jsonExportCanOmitShotsAndImportAcceptsWrapperObject() {
        const QByteArray shared = QJsonDocument(QJsonObject{{"cases", formats::casesToJson({richCase()}, /*includeShots=*/false)}}).toJson();
        const auto back = formats::casesFromJson(shared);
        QVERIFY(back.has_value());
        QVERIFY(back->first().shots.isEmpty());
    }

    void jsonImportRejectsInvalidInput() {
        QString err;
        QVERIFY(!formats::casesFromJson("{not json", &err).has_value());
        QVERIFY(!err.isEmpty());
        QVERIFY(!formats::casesFromJson("{\"foo\": 1}", &err).has_value());
        QVERIFY(formats::casesFromJson("[{\"title\": \"sin id\"}]")->isEmpty());   // sin id se descarta
    }

    // ---- CSV ---------------------------------------------------------------------------

    void csvHasOneRowPerStepAndRoundTrips() {
        TestCase noSteps;
        noSteps.id = QStringLiteral("TC-2"); noSteps.title = QStringLiteral("Sin pasos");
        const QString csv = formats::casesToCsv({richCase(), noSteps});
        QVERIFY(csv.startsWith(QStringLiteral("id,title,suite,priority,status,tags,component,jira,preconditions,step,action,expected\n")));

        const auto back = formats::casesFromCsv(csv);
        QVERIFY(back.has_value());
        QCOMPARE(back->size(), 2);
        const TestCase& r = back->first();
        QCOMPARE(r.title, QStringLiteral("Título, con coma"));       // comas y comillas escapadas
        QCOMPARE(r.steps[0].action, QStringLiteral("Pulsar \"OK\""));
        QCOMPARE(r.preconditions, QStringLiteral("línea 1\nlínea 2")); // saltos de línea entre comillas
        QCOMPARE(r.tags, (QStringList{QStringLiteral("smoke"), QStringLiteral("api")}));
        QCOMPARE(r.steps.size(), 2);
        QCOMPARE(static_cast<int>(r.priority), static_cast<int>(Priority::Alta));
        QVERIFY(back->last().steps.isEmpty());
    }

    void csvImportNeedsIdAndTitleColumns() {
        QString err;
        QVERIFY(!formats::casesFromCsv(QStringLiteral("foo,bar\n1,2\n"), &err).has_value());
        QVERIFY(err.contains(QStringLiteral("id")));
        QVERIFY(!formats::casesFromCsv(QString(), &err).has_value());
    }

    // ---- Markdown ----------------------------------------------------------------------

    void markdownEscapesPipesAndListsMetadata() {
        TestCase c;
        c.id = QStringLiteral("TC-1"); c.title = QStringLiteral("Login"); c.suite = QStringLiteral("Auth"); c.jiraKey = QStringLiteral("SHOP-3");
        c.steps = {TestStep{QStringLiteral("Abrir | pantalla"), QStringLiteral("Se ve")}};
        const QString md = formats::casesToMarkdown({c});
        QVERIFY(md.contains(QStringLiteral("## TC-1 · Login")));
        QVERIFY(md.contains(QStringLiteral("**Historia:** SHOP-3")));
        QVERIFY(md.contains(QStringLiteral("| 1 | Abrir \\| pantalla | Se ve |")));
    }
};

QTEST_APPLESS_MAIN(CaseFormatsTest)
#include "test_case_formats.moc"
