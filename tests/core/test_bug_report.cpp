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
    b.stepsToReproduce = {QStringLiteral("abrir"), QStringLiteral("pagar")};
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

    void classificationsAreTheOnesPrintedInTheQualityForm() {
        QCOMPARE(BugReport::classifications(), QStringList({QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"),
                                                            QStringLiteral("D"), QStringLiteral("E")}));
        QCOMPARE(BugReport::classificationName(QStringLiteral("A")), QStringLiteral("Funcionamiento/Lógica"));
        QCOMPARE(BugReport::classificationName(QStringLiteral("E")), QStringLiteral("Vulnerabilidades"));
        QVERIFY(BugReport::classificationName(QStringLiteral("Z")).isEmpty());
        QCOMPARE(BugReport::classificationLabel(QStringLiteral("B")), QStringLiteral("B · Datos"));
        QCOMPARE(BugReport::classificationLabel(QStringLiteral("Z")), QStringLiteral("Z"));
        QCOMPARE(BugReport().classification, QStringLiteral("A"));   // lo más común: funcionamiento
    }

    void theClassificationTravelsInTheDescription() {
        BugReport b = sample();
        b.classification = QStringLiteral("C");
        QVERIFY(b.jiraDescription().contains(QStringLiteral("h3. Clasificación\nC · Estético/Forma")));
        QVERIFY(b.markdownDescription().contains(QStringLiteral("**Clasificación:** C · Estético/Forma")));
        QVERIFY(b.htmlDescription().contains(QStringLiteral("<b>Clasificación:</b> C · Estético/Forma")));
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

    /// Los pasos van como lista numerada de cada gestor, y los usuarios usados, en su sección.
    void stepsAndTestUsersTravelInEveryDescription() {
        BugReport b = sample();
        b.stepsToReproduce << QStringLiteral("  ");   // una fila vacía no cuenta
        b.testUsers = {TestUser{QStringLiteral("qa.cajero"), QStringLiteral("Cajero")}, TestUser{QStringLiteral("qa.admin"), {}}};
        QVERIFY(b.jiraDescription().contains(QStringLiteral("h3. Pasos para reproducir\n# abrir\n# pagar\n\n")));
        QVERIFY(b.jiraDescription().contains(QStringLiteral("h3. Usuarios utilizados\n* qa.cajero (Cajero)\n* qa.admin\n")));
        QVERIFY(b.markdownDescription().contains(QStringLiteral("### Usuarios utilizados\n- qa.cajero (Cajero)\n- qa.admin")));
        QVERIFY(b.htmlDescription().contains(QStringLiteral("<h3>Pasos para reproducir</h3><ol><li>abrir</li><li>pagar</li></ol>")));
        QVERIFY(b.htmlDescription().contains(QStringLiteral("<ul><li>qa.cajero (Cajero)</li><li>qa.admin</li></ul>")));
        b.testUsers.clear();
        b.stepsToReproduce.clear();
        QVERIFY(!b.jiraDescription().contains(QStringLiteral("Usuarios utilizados")));
        QVERIFY(b.jiraDescription().contains(QStringLiteral("h3. Pasos para reproducir\n—")));
    }

    /// Cada proyecto llama al bug a su manera: se propone el que tiene, con su ortografía.
    void defaultIssueTypeIsTheOneTheProjectHas() {
        QCOMPARE(BugReport::defaultIssueType({}), QStringLiteral("Bug"));
        QCOMPARE(BugReport::defaultIssueType({QStringLiteral("Tarea"), QStringLiteral("error"), QStringLiteral("Mejora")}), QStringLiteral("error"));
        QCOMPARE(BugReport::defaultIssueType({QStringLiteral("Historia"), QStringLiteral("Bug")}), QStringLiteral("Bug"));
        QCOMPARE(BugReport::defaultIssueType({QStringLiteral("Improvement"), QStringLiteral("Incidencia")}), QStringLiteral("Incidencia"));
    }

    /// Lo que «Traer de Jira» busca: los tipos de bug y de mejora que el proyecto tiene, con su nombre.
    void trackedIssueTypesAreTheProjectOnes() {
        QCOMPARE(BugReport::trackedIssueTypes({}), BugReport::jiraIssueTypes());
        QCOMPARE(BugReport::trackedIssueTypes({QStringLiteral("Tarea"), QStringLiteral("error"), QStringLiteral("Mejora")}),
                 (QStringList{QStringLiteral("error"), QStringLiteral("Mejora")}));
        QCOMPARE(BugReport::trackedIssueTypes({QStringLiteral("Bug"), QStringLiteral("Defect"), QStringLiteral("Improvement"), QStringLiteral("Story")}),
                 (QStringList{QStringLiteral("Bug"), QStringLiteral("Defect"), QStringLiteral("Improvement")}));
        // Sin ninguno conocido, el que se propone al reportar: es con el que se crean.
        QCOMPARE(BugReport::trackedIssueTypes({QStringLiteral("Incidencia"), QStringLiteral("Historia")}), QStringList{QStringLiteral("Incidencia")});
    }

    void titleFromActualTakesTheFirstSentence() {
        QCOMPARE(BugReport::titleFromActual(QStringLiteral("Facturación"), QStringLiteral("aparece «Error 500» al emitir. Luego se cuelga.\nMás")),
                 QStringLiteral("[Facturación] Aparece «Error 500» al emitir"));
        QCOMPARE(BugReport::titleFromActual(QString(), QStringLiteral("  la versión 2.3 no carga  ")), QStringLiteral("La versión 2.3 no carga"));
        QVERIFY(BugReport::titleFromActual(QStringLiteral("S"), QStringLiteral("   ")).isEmpty());
        const QString longOne = BugReport::titleFromActual(QString(), QString(QStringLiteral("palabra ")).repeated(20));
        QVERIFY(longOne.size() <= 91);
        QVERIFY(longOne.endsWith(QStringLiteral("…")));
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
