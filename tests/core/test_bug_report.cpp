// BugReport (core/models/BugReport.h): validación y descripciones por gestor.

#include "core/models/BugReport.h"

#include <QtTest>

using namespace qaflow;

namespace {
BugReport sample() {
    BugReport b;
    b.title = QStringLiteral("t");
    b.actual = QStringLiteral("se rompe");
    b.expected = QStringLiteral("funciona");
    b.stepsToReproduce = QStringLiteral("1. abrir\n2. pagar");
    b.linkedCaseId = QStringLiteral("TC-104");
    b.environment = QStringLiteral("QA");
    b.severity = QStringLiteral("Crítica");
    return b;
}
} // namespace

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

    void jiraPrioritySuggestedFromSeverity() {
        QCOMPARE(BugReport::jiraPriorityFor(QStringLiteral("Bloqueante")), QStringLiteral("Highest"));
        QCOMPARE(BugReport::jiraPriorityFor(QStringLiteral("Mayor")), QStringLiteral("Medium"));
        QCOMPARE(BugReport::jiraPriorityFor(QStringLiteral("Trivial")), QStringLiteral("Lowest"));
        QVERIFY(BugReport::jiraPriorityFor(QStringLiteral("???")).isEmpty());
    }

    void jiraDescriptionHasSectionsAndLinkedStory() {
        BugReport b = sample();
        const QString plain = b.jiraDescription();
        QVERIFY(plain.contains(QStringLiteral("h3. Resultado actual\nse rompe")));
        QVERIFY(plain.contains(QStringLiteral("h3. Severidad\nCrítica")));
        QVERIFY(plain.contains(QStringLiteral("TC-104")));
        QVERIFY(!plain.contains(QStringLiteral("Historia relacionada")));
        b.linkedStoryKey = QStringLiteral("SHOP-12");
        QVERIFY(b.jiraDescription().contains(QStringLiteral("h3. Historia relacionada\nSHOP-12")));
    }

    void markdownDescriptionListsAttachmentLinks() {
        const QString md = sample().markdownDescription({QStringLiteral("![cap](/uploads/x/cap.png)")});
        QVERIFY(md.startsWith(QStringLiteral("**Entorno:** QA · **Severidad:** Crítica")));
        QVERIFY(md.contains(QStringLiteral("### Pasos para reproducir\n1. abrir\n2. pagar")));
        QVERIFY(md.contains(QStringLiteral("### Capturas\n![cap](/uploads/x/cap.png)")));
        QVERIFY(!sample().markdownDescription().contains(QStringLiteral("### Capturas")));
    }

    void htmlDescriptionEscapesAndBreaksLines() {
        BugReport b = sample();
        b.actual = QStringLiteral("<b>x</b>\ny");
        const QString html = b.htmlDescription();
        QVERIFY(html.contains(QStringLiteral("&lt;b&gt;x&lt;/b&gt;<br>y")));
        QVERIFY(html.contains(QStringLiteral("<h3>Resultado esperado</h3><p>funciona</p>")));
    }
};

QTEST_APPLESS_MAIN(BugReportTest)
#include "test_bug_report.moc"
