// MarkupEditorDialog (presentation/widgets): editor con formato de los campos de los pasos. Los botones
// escriben el marcado wiki de Jira que acepta Zephyr y la vista previa lo enseña con formato. Se ejecuta
// con la plataforma "offscreen".

#include "presentation/widgets/MarkupEditorDialog.h"
#include "presentation/widgets/TextArea.h"

#include <QFontDatabase>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QVBoxLayout>
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

    /// El alto que se pide para el texto cuenta el interlineado de la letra, como lo coloca el campo: con
    /// una letra que lo tiene (Segoe UI, la de Windows) el texto se quedaba corto y se desplazaba por dentro.
    void autoHeightFitsTheTextWithAFontThatHasLeading() {
        // La de Windows si está; si no, la letra latina con más interlineado (no una de símbolos, que ni
        // siquiera tiene las letras del texto): cuanto más tiene, más se nota si no se cuenta.
        QFont font;
        const QStringList families = QFontDatabase::families(QFontDatabase::Latin);
        if (families.contains(QStringLiteral("Segoe UI"))) {
            font = QFont(QStringLiteral("Segoe UI"));
            font.setPixelSize(13);
        } else {
            for (const QString& family : families) {
                if (QFontDatabase::isPrivateFamily(family)) continue;
                QFont candidate(family);
                candidate.setPixelSize(13);
                if (QFontMetrics(candidate).leading() > QFontMetrics(font).leading()) font = candidate;
            }
        }
        if (QFontMetrics(font).leading() <= 0) QSKIP("No hay ninguna letra con interlineado instalada");
        // En un layout, como en el formulario del caso: es él quien le da el alto que pide para su ancho.
        QWidget container;
        auto* layout = new QVBoxLayout(&container);
        auto* area = new TextArea(2);
        area->setFont(font);
        // Sin un tope que el texto alcance: pasado el tope, desplazarse por dentro es lo esperado.
        area->setAutoHeight(1, 60);
        area->setPlainText(QStringLiteral("Abrir el detalle del pedido y comprobar el importe ").repeated(6));
        layout->addWidget(area);
        layout->addStretch(1);
        container.resize(320, 600);
        container.show();
        QVERIFY(QTest::qWaitForWindowExposed(&container));
        QTRY_COMPARE(area->verticalScrollBar()->maximum(), 0);   // se ve entero
    }
};

QTEST_MAIN(TestMarkupEditor)
#include "test_markup_editor.moc"
