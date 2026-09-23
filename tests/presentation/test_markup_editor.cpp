// MarkupEditorDialog (presentation/widgets): editor con formato de los campos de los pasos. Los botones
// escriben el marcado wiki de Jira que acepta Zephyr y la vista previa lo enseña con formato. Se ejecuta
// con la plataforma "offscreen".

#include "presentation/widgets/MarkupEditorDialog.h"
#include "presentation/widgets/TextArea.h"

#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTextBrowser>
#include <QtTest>

using namespace qaflow;

namespace {
QPlainTextEdit* source(MarkupEditorDialog& d) { return d.findChild<QPlainTextEdit*>(QStringLiteral("markupSource")); }

void select(QPlainTextEdit* e, int from, int to) {
    QTextCursor c = e->textCursor();
    c.setPosition(from);
    c.setPosition(to, QTextCursor::KeepAnchor);
    e->setTextCursor(c);
}
} // namespace

class TestMarkupEditor : public QObject {
    Q_OBJECT
private slots:
    void wrapsTheSelectionAndKeepsItSelected() {
        MarkupEditorDialog d(QStringLiteral("Paso 1"), QStringLiteral("pulsar Aceptar"));
        select(source(d), 7, 14);
        d.wrap(QStringLiteral("*"), QStringLiteral("*"), QStringLiteral("texto"));
        QCOMPARE(d.text(), QStringLiteral("pulsar *Aceptar*"));
        QCOMPARE(source(d)->textCursor().selectedText(), QStringLiteral("Aceptar"));
    }

    void withoutSelectionInsertsAPlaceholder() {
        MarkupEditorDialog d(QStringLiteral("Paso 1"), QString());
        d.wrap(QStringLiteral("{{"), QStringLiteral("}}"), QStringLiteral("valor"));
        QCOMPARE(d.text(), QStringLiteral("{{valor}}"));
        QCOMPARE(source(d)->textCursor().selectedText(), QStringLiteral("valor"));
    }

    void listButtonTogglesTheMarkerOnEverySelectedLine() {
        MarkupEditorDialog d(QStringLiteral("Paso 1"), QStringLiteral("uno\n# dos\ntres"));
        select(source(d), 0, d.text().size());
        d.prefixLines(QStringLiteral("*"));
        QCOMPARE(d.text(), QStringLiteral("* uno\n* dos\n* tres"));
        select(source(d), 0, d.text().size());
        d.prefixLines(QStringLiteral("*"));
        QCOMPARE(d.text(), QStringLiteral("uno\ndos\ntres"));
    }

    void spreadsheetSelectionBecomesATableOnItsOwnLines() {
        MarkupEditorDialog d(QStringLiteral("Paso 1"), QStringLiteral("Datos: usuario\tclave\nana\t123"));
        select(source(d), 7, d.text().size());
        d.insertTable(3, 3);
        QCOMPARE(d.text(), QStringLiteral("Datos: \n||usuario||clave||\n|ana|123|"));
    }

    void previewRendersTheMarkup() {
        MarkupEditorDialog d(QStringLiteral("Paso 1"), QStringLiteral("||a||b||\n|1|2|"));
        auto* preview = d.findChild<QTextBrowser*>(QStringLiteral("markupPreview"));
        QVERIFY(preview);
        QVERIFY(preview->toHtml().contains(QStringLiteral("<table")));
        source(d)->setPlainText(QStringLiteral("*fuerte*"));
        QVERIFY(!preview->toHtml().contains(QStringLiteral("<table")));
        QVERIFY(preview->toPlainText().contains(QStringLiteral("fuerte")));
        QVERIFY(!preview->toPlainText().contains(QLatin1Char('*')));
    }

    void textAreaOffersTheEditorOnlyWhenEnabled() {
        TextArea plain(2);
        QVERIFY(!plain.findChild<QPushButton*>(QStringLiteral("markupButton")));
        TextArea rich(2);
        rich.enableMarkupEditor(QStringLiteral("Paso 1 · Acción"));
        QVERIFY(rich.findChild<QPushButton*>(QStringLiteral("markupButton")));
    }
};

QTEST_MAIN(TestMarkupEditor)
#include "test_markup_editor.moc"
