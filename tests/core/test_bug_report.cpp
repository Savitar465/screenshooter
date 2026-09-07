// BugReport (core/models/BugReport.h): validación y descripción para Jira.

#include "core/models/BugReport.h"

#include <QtTest>

using namespace qaflow;

class BugReportTest : public QObject {
    Q_OBJECT
private slots:
    void isValidNeedsTitleAndActualResult() {
        BugReport b;
        QVERIFY(!b.isValid());
        b.title = QStringLiteral("t");
        QVERIFY(!b.isValid());
        b.actual = QStringLiteral("a");
        QVERIFY(b.isValid());
        b.title = QStringLiteral("   ");
        QVERIFY(!b.isValid());    // sólo espacios no cuenta
    }

    void jiraDescriptionHasSectionsAndLinkedStory() {
        BugReport b;
        b.title = QStringLiteral("t");
        b.actual = QStringLiteral("se rompe");
        b.linkedCaseId = QStringLiteral("TC-104");
        const QString plain = b.jiraDescription();
        QVERIFY(plain.contains(QStringLiteral("h3. Resultado actual")));
        QVERIFY(plain.contains(QStringLiteral("TC-104")));
        QVERIFY(!plain.contains(QStringLiteral("Historia relacionada")));
        b.linkedStoryKey = QStringLiteral("SHOP-12");
        QVERIFY(b.jiraDescription().contains(QStringLiteral("h3. Historia relacionada\nSHOP-12")));
    }
};

QTEST_APPLESS_MAIN(BugReportTest)
#include "test_bug_report.moc"
