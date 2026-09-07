// TestCase, LastRun y utilidades de etiquetas y enumeraciones (core/models/TestCase.h).

#include "core/models/TestCase.h"

#include <QtTest>

using namespace qaflow;

class TestCaseTest : public QObject {
    Q_OBJECT
private slots:
    // ---- LastRun -----------------------------------------------------------------------

    void lastRunLabelIsRelativeToNow() {
        const QDateTime now(QDate(2026, 9, 7), QTime(12, 0));
        QCOMPARE(LastRun{}.label(now), QStringLiteral("Sin ejecutar"));
        QCOMPARE((LastRun{RunOutcome::Passed, now.addSecs(-30)}).label(now), QStringLiteral("Pasó · ahora"));
        QCOMPARE((LastRun{RunOutcome::Failed, now.addSecs(-600)}).label(now), QStringLiteral("Falló · hace 10 min"));
        QCOMPARE((LastRun{RunOutcome::Blocked, now.addSecs(-7200)}).label(now), QStringLiteral("Bloqueado · hace 2 h"));
        QCOMPARE((LastRun{RunOutcome::Passed, now.addDays(-1)}).label(now), QStringLiteral("Pasó · ayer"));
        QCOMPARE((LastRun{RunOutcome::Passed, now.addDays(-2)}).label(now), QStringLiteral("Pasó · hace 2 d"));
    }

    // ---- TestCase ----------------------------------------------------------------------

    void readyToBeMarkedListoNeedsTitleAndCompleteSteps() {
        TestCase c;
        QVERIFY(!c.readyToBeMarkedListo());
        c.title = QStringLiteral("x");
        QVERIFY(!c.readyToBeMarkedListo());            // sin pasos
        c.steps.append(TestStep{QStringLiteral("a"), QString()});
        QVERIFY(!c.readyToBeMarkedListo());            // paso incompleto
        c.steps[0].expected = QStringLiteral("b");
        QVERIFY(c.readyToBeMarkedListo());
    }

    void unassignedShotsCountsStepZero() {
        TestCase c;
        c.shots = {Screenshot{1, 0, {}, {}}, Screenshot{2, 2, {}, {}}, Screenshot{3, 0, {}, {}}};
        QCOMPARE(c.unassignedShots(), 2);
    }

    void searchTextCoversIdTitleSuiteMetadataAndTags() {
        TestCase c;
        c.id = QStringLiteral("TC-7"); c.title = QStringLiteral("Pago"); c.suite = QStringLiteral("Checkout");
        c.component = QStringLiteral("Carrito"); c.jiraKey = QStringLiteral("SHOP-12"); c.tags = {QStringLiteral("Smoke")};
        const QString s = c.searchText();
        for (const auto& needle : {"tc-7", "pago", "checkout", "carrito", "shop-12", "smoke"}) QVERIFY2(s.contains(QLatin1String(needle)), needle);
    }

    // ---- Funciones libres --------------------------------------------------------------

    void parseTagsTrimsDeduplicatesAndDropsEmpties() {
        QCOMPARE(parseTags(QStringLiteral(" smoke, regresión ,, Smoke ,api")), (QStringList{QStringLiteral("smoke"), QStringLiteral("regresión"), QStringLiteral("api")}));
        QVERIFY(parseTags(QString()).isEmpty());
    }

    void priorityAndStatusRoundTripThroughStrings() {
        for (auto p : {Priority::Alta, Priority::Media, Priority::Baja}) QCOMPARE(static_cast<int>(priorityFromString(toString(p))), static_cast<int>(p));
        for (auto s : {CaseStatus::Listo, CaseStatus::Borrador, CaseStatus::Obsoleto}) QCOMPARE(static_cast<int>(statusFromString(toString(s))), static_cast<int>(s));
        QCOMPARE(static_cast<int>(priorityFromString(QStringLiteral("???"))), static_cast<int>(Priority::Media));   // valor por defecto
        QCOMPARE(static_cast<int>(statusFromString(QString())), static_cast<int>(CaseStatus::Borrador));
    }
};

QTEST_APPLESS_MAIN(TestCaseTest)
#include "test_test_case.moc"
