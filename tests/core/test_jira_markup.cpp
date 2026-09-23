// JiraMarkup (core/JiraMarkup.h): el marcado wiki de Jira que llevan los pasos se enseña con formato en
// la vista previa del editor, y las tablas se pueden generar a partir de lo copiado de una hoja de cálculo.

#include "core/JiraMarkup.h"

#include <QtTest>

using namespace qaflow;

class JiraMarkupTest : public QObject {
    Q_OBJECT
private slots:
    void inlineMarksBecomeTags() {
        const QString html = jira::toHtml(QStringLiteral("*negrita* _cursiva_ +sub+ -tachado- {{mono}}"));
        QVERIFY(html.contains(QStringLiteral("<b>negrita</b>")));
        QVERIFY(html.contains(QStringLiteral("<i>cursiva</i>")));
        QVERIFY(html.contains(QStringLiteral("<u>sub</u>")));
        QVERIFY(html.contains(QStringLiteral("<s>tachado</s>")));
        QVERIFY(html.contains(QStringLiteral(">mono</code>")));
    }

    void marksNeedToHugTheirText() {
        const QString html = jira::toHtml(QStringLiteral("a - b - c y 2 * 3 * 4 y snake_case_name"));
        QVERIFY(!html.contains(QStringLiteral("<s>")));
        QVERIFY(!html.contains(QStringLiteral("<b>")));
        QVERIFY(!html.contains(QStringLiteral("<i>")));
    }

    void htmlIsEscapedAndMonoIsLiteral() {
        const QString html = jira::toHtml(QStringLiteral("<script>x</script> {{*no negrita*}}"));
        QVERIFY(!html.contains(QStringLiteral("<script>")));
        QVERIFY(html.contains(QStringLiteral("&lt;script&gt;")));
        QVERIFY(html.contains(QStringLiteral(">*no negrita*</code>")));
    }

    void tablesWithHeaderAndEscapedBars() {
        const QString html = jira::toHtml(QStringLiteral("||Usuario||Clave||\n|ana|a\\|b|\n|[doc|https://x.io/a]|{{p|q}}|"));
        QCOMPARE(html.count(QStringLiteral("<table")), 1);
        QCOMPARE(html.count(QStringLiteral("<th")), 2);
        QCOMPARE(html.count(QStringLiteral("<td>")), 4);
        QVERIFY(html.contains(QStringLiteral("<td>a|b</td>")));
        QVERIFY(html.contains(QStringLiteral("href=\"https://x.io/a\"")));
        QVERIFY(html.contains(QStringLiteral(">p|q</code>")));
    }

    void nestedListsOpenAndCloseInOrder() {
        const QString html = jira::toHtml(QStringLiteral("# uno\n#* a\n# dos\n\ntexto"));
        QCOMPARE(html.count(QStringLiteral("<ol>")), 1);
        QCOMPARE(html.count(QStringLiteral("<ul>")), 1);
        QVERIFY(html.indexOf(QStringLiteral("</ul>")) < html.indexOf(QStringLiteral("dos")));
        QVERIFY(html.indexOf(QStringLiteral("</ol>")) < html.indexOf(QStringLiteral("<p>texto")));
    }

    void blocksHeadingsAndColor() {
        const QString html = jira::toHtml(QStringLiteral("h3. Título\n{code}\n*literal*\n{code}\n{color:red}rojo{color}\\\\siguiente"));
        QVERIFY(html.contains(QStringLiteral("<h3>Título</h3>")));
        QVERIFY(html.contains(QStringLiteral(">*literal*</pre>")));
        QVERIFY(html.contains(QStringLiteral("<span style=\"color:red;\">rojo</span><br/>siguiente")));
    }

    void unsafeLinksAreNotLinks() {
        const QString html = jira::toHtml(QStringLiteral("[x|javascript:alert(1)]"));
        QVERIFY(!html.contains(QStringLiteral("href")));
    }

    void tableFromSpreadsheetPaste() {
        QCOMPARE(jira::tableFromTsv(QStringLiteral("a\tb\n1\t\n3\tx|y\n")),
                 QStringLiteral("||a||b||\n|1| |\n|3|x\\|y|"));
        QCOMPARE(jira::tableFromTsv(QStringLiteral("1\t2"), false), QStringLiteral("|1|2|"));
    }

    void emptyTableHasHeaderAndRows() {
        const QStringList lines = jira::emptyTable(2, 3).split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), 3);
        QVERIFY(lines[0].startsWith(QStringLiteral("||")));
        QCOMPARE(lines[1], QStringLiteral("| | | |"));
    }
};

QTEST_GUILESS_MAIN(JiraMarkupTest)
#include "test_jira_markup.moc"
