#include "MarkupEditorDialog.h"

#include "core/JiraMarkup.h"
#include "presentation/theme/Theme.h"
#include "presentation/widgets/Ui.h"

#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QScrollBar>
#include <QShortcut>
#include <QSpinBox>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>

namespace qaflow {

namespace {
QPushButton* toolButton(const QString& text, const QString& tip, QWidget* parent) {
    auto* b = ui::button(text, "outline", parent);
    b->setToolTip(tip);
    b->setFocusPolicy(Qt::NoFocus);   // el foco se queda en el texto, con su selección
    b->setStyleSheet(QStringLiteral("padding:4px 9px;font-size:12px;border-radius:6px;min-width:18px;"));
    return b;
}

/// Colores con nombre que Jira entiende en {color:…}.
struct NamedColor { const char* name; const char* label; };
constexpr NamedColor kColors[] = {
    {"red", QT_TRANSLATE_NOOP("MarkupEditorDialog", "Rojo")},
    {"green", QT_TRANSLATE_NOOP("MarkupEditorDialog", "Verde")},
    {"blue", QT_TRANSLATE_NOOP("MarkupEditorDialog", "Azul")},
    {"orange", QT_TRANSLATE_NOOP("MarkupEditorDialog", "Naranja")},
    {"gray", QT_TRANSLATE_NOOP("MarkupEditorDialog", "Gris")},
};
} // namespace

MarkupEditorDialog::MarkupEditorDialog(const QString& title, const QString& text, QWidget* parent) : QDialog(parent) {
    setWindowTitle(title);
    resize(960, 560);
    auto* v = ui::vbox(this, 16, 10);

    // Barra de formato
    auto* bar = new QWidget;
    auto* bh = ui::hbox(bar, 0, 4);
    auto add = [&](const QString& label, const QString& tip, const std::function<void()>& action) {
        auto* b = toolButton(label, tip, bar);
        connect(b, &QPushButton::clicked, this, action);
        bh->addWidget(b);
        return b;
    };
    auto* bold = add(QStringLiteral("B"), tr("Negrita (Ctrl+B)"), [this]() { wrap(QStringLiteral("*"), QStringLiteral("*"), tr("texto")); });
    bold->setStyleSheet(bold->styleSheet() + QStringLiteral("font-weight:800;"));
    auto* italic = add(QStringLiteral("I"), tr("Cursiva (Ctrl+I)"), [this]() { wrap(QStringLiteral("_"), QStringLiteral("_"), tr("texto")); });
    italic->setStyleSheet(italic->styleSheet() + QStringLiteral("font-style:italic;"));
    auto* under = add(QStringLiteral("U"), tr("Subrayado (Ctrl+U)"), [this]() { wrap(QStringLiteral("+"), QStringLiteral("+"), tr("texto")); });
    under->setStyleSheet(under->styleSheet() + QStringLiteral("text-decoration:underline;"));
    auto* strike = add(QStringLiteral("S"), tr("Tachado"), [this]() { wrap(QStringLiteral("-"), QStringLiteral("-"), tr("texto")); });
    strike->setStyleSheet(strike->styleSheet() + QStringLiteral("text-decoration:line-through;"));
    add(QStringLiteral("{ }"), tr("Monoespaciado: valores, rutas, comandos"), [this]() { wrap(QStringLiteral("{{"), QStringLiteral("}}"), tr("valor")); });

    auto* color = toolButton(tr("Color"), tr("Color del texto"), bar);
    auto* colorMenu = new QMenu(color);
    for (const auto& c : kColors) {
        const QString name = QString::fromLatin1(c.name);
        colorMenu->addAction(tr(c.label), this, [this, name]() { wrap(QStringLiteral("{color:%1}").arg(name), QStringLiteral("{color}"), tr("texto")); });
    }
    colorMenu->addSeparator();
    colorMenu->addAction(tr("Otro…"), this, &MarkupEditorDialog::pickColor);
    color->setMenu(colorMenu);
    bh->addWidget(color);

    auto* heading = toolButton(tr("Título"), tr("Título de sección"), bar);
    auto* headingMenu = new QMenu(heading);
    for (int level = 1; level <= 4; ++level) {
        headingMenu->addAction(tr("Título %1").arg(level), this, [this, level]() { prefixLines(QStringLiteral("h%1.").arg(level)); });
    }
    heading->setMenu(headingMenu);
    bh->addWidget(heading);

    bh->addSpacing(8);
    add(QStringLiteral("• ") + tr("Lista"), tr("Lista con viñetas"), [this]() { prefixLines(QStringLiteral("*")); });
    add(QStringLiteral("1. ") + tr("Lista"), tr("Lista numerada"), [this]() { prefixLines(QStringLiteral("#")); });

    auto* table = toolButton(tr("Tabla"), tr("Insertar una tabla o convertir en tabla lo copiado de una hoja de cálculo"), bar);
    auto* tableMenu = new QMenu(table);
    tableMenu->addAction(tr("Insertar tabla…"), this, &MarkupEditorDialog::askTable);
    tableMenu->addAction(tr("Pegar como tabla (desde Excel / hoja de cálculo)"), this, [this]() {
        const QString clip = QApplication::clipboard()->text();
        if (!clip.trimmed().isEmpty()) insertBlock(jira::tableFromTsv(clip));
    });
    table->setMenu(tableMenu);
    bh->addWidget(table);
    add(tr("Enlace"), tr("Enlace a una URL"), [this]() { askLink(); });
    add(tr("Código"), tr("Bloque de código o texto sin formato"), [this]() { wrap(QStringLiteral("{code}\n"), QStringLiteral("\n{code}"), tr("código")); });
    add(QStringLiteral("↵"), tr("Salto de línea dentro de una celda de tabla"), [this]() { m_edit->insertPlainText(QStringLiteral("\\\\")); });
    bh->addStretch(1);
    v->addWidget(bar);

    // Texto y vista previa
    auto* split = new QSplitter(Qt::Horizontal);
    auto* left = new QWidget;
    auto* lv = ui::vbox(left, 0, 6);
    lv->addWidget(ui::label(tr("MARCADO DE JIRA"), "eyebrow"));
    m_edit = new QPlainTextEdit;
    m_edit->setObjectName(QStringLiteral("markupSource"));
    m_edit->setProperty("role", QStringLiteral("panel"));
    m_edit->setTabChangesFocus(false);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    m_edit->setFont(mono);
    m_edit->setPlainText(text);
    lv->addWidget(m_edit, 1);
    split->addWidget(left);

    auto* right = new QWidget;
    auto* rv = ui::vbox(right, 0, 6);
    rv->addWidget(ui::label(tr("VISTA PREVIA · así se ve en Zephyr"), "eyebrow"));
    m_preview = new QTextBrowser;
    m_preview->setObjectName(QStringLiteral("markupPreview"));
    m_preview->setOpenExternalLinks(true);
    m_preview->setProperty("role", QStringLiteral("panel"));
    rv->addWidget(m_preview, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    v->addWidget(split, 1);

    auto* hint = ui::label(tr("*negrita*  _cursiva_  +subrayado+  -tachado-  {{mono}}  ||cabecera||  |celda|  * viñeta  # número  [texto|https://…]"), "muted-sm");
    hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    v->addWidget(hint);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("Aplicar"));
    buttons->button(QDialogButtonBox::Ok)->setToolTip(tr("Ctrl+Intro"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("Cancelar"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    v->addWidget(buttons);

    auto shortcut = [this](const QKeySequence& key, const std::function<void()>& action) {
        auto* s = new QShortcut(key, this);
        connect(s, &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_B), [this]() { wrap(QStringLiteral("*"), QStringLiteral("*"), tr("texto")); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_I), [this]() { wrap(QStringLiteral("_"), QStringLiteral("_"), tr("texto")); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_U), [this]() { wrap(QStringLiteral("+"), QStringLiteral("+"), tr("texto")); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), [this]() { accept(); });
    shortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), [this]() { accept(); });

    connect(m_edit, &QPlainTextEdit::textChanged, this, &MarkupEditorDialog::updatePreview);
    updatePreview();
    m_edit->setFocus();
    m_edit->moveCursor(QTextCursor::End);
}

QString MarkupEditorDialog::text() const {
    return m_edit->toPlainText();
}

void MarkupEditorDialog::wrap(const QString& open, const QString& close, const QString& placeholder) {
    QTextCursor c = m_edit->textCursor();
    const QString selected = c.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    const QString inner = selected.isEmpty() ? placeholder : selected;
    c.beginEditBlock();
    const int start = c.selectionStart();
    c.insertText(open + inner + close);
    c.endEditBlock();
    // Queda seleccionado lo que va dentro de la marca, para seguir escribiendo o aplicar otra.
    c.setPosition(start + open.size());
    c.setPosition(start + open.size() + inner.size(), QTextCursor::KeepAnchor);
    m_edit->setTextCursor(c);
    m_edit->setFocus();
}

void MarkupEditorDialog::prefixLines(const QString& marker) {
    QTextCursor c = m_edit->textCursor();
    QTextBlock first = m_edit->document()->findBlock(c.selectionStart());
    const QTextBlock last = m_edit->document()->findBlock(c.selectionEnd());
    // Una línea con otra marca de bloque la pierde; con la misma, se quita (el botón alterna).
    static const QRegularExpression existing(QStringLiteral("^\\s*(?:h[1-6]\\.|bq\\.|[*#]+|-)\\s+"));
    const QString wanted = marker + QLatin1Char(' ');
    bool allHaveIt = true;
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        if (!b.text().startsWith(wanted)) allHaveIt = false;
        if (b == last) break;
    }
    c.beginEditBlock();
    for (QTextBlock b = first; b.isValid(); b = b.next()) {
        QString line = b.text();
        const auto m = existing.match(line);
        if (m.hasMatch()) line = line.mid(m.capturedEnd());
        if (!allHaveIt) line = wanted + line;
        QTextCursor lc(b);
        lc.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        lc.insertText(line);
        if (b == last) break;
    }
    c.endEditBlock();
    m_edit->setFocus();
}

void MarkupEditorDialog::insertTable(int rows, int cols) {
    QTextCursor c = m_edit->textCursor();
    const QString selected = c.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    insertBlock(selected.contains(QLatin1Char('\t')) ? jira::tableFromTsv(selected) : jira::emptyTable(rows, cols));
}

void MarkupEditorDialog::insertBlock(const QString& block) {
    QTextCursor c = m_edit->textCursor();
    c.beginEditBlock();
    c.removeSelectedText();
    // La tabla va en sus propias líneas: si no, Jira la lee como texto.
    QString before, after;
    if (!c.atBlockStart()) before = QStringLiteral("\n");
    if (!c.atBlockEnd()) after = QStringLiteral("\n");
    c.insertText(before + block + after);
    c.endEditBlock();
    m_edit->setTextCursor(c);
    m_edit->setFocus();
}

void MarkupEditorDialog::askTable() {
    QTextCursor c = m_edit->textCursor();
    if (c.selectedText().contains(QLatin1Char('\t'))) { insertTable(0, 0); return; }
    QDialog d(this);
    d.setWindowTitle(tr("Insertar tabla"));
    auto* form = new QFormLayout(&d);
    auto* rows = new QSpinBox;
    rows->setRange(1, 50);
    rows->setValue(3);
    auto* cols = new QSpinBox;
    cols->setRange(1, 12);
    cols->setValue(3);
    form->addRow(tr("Filas (sin la cabecera)"), rows);
    form->addRow(tr("Columnas"), cols);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    form->addRow(buttons);
    if (d.exec() != QDialog::Accepted) return;
    insertTable(rows->value(), cols->value());
}

void MarkupEditorDialog::askLink() {
    QTextCursor c = m_edit->textCursor();
    QDialog d(this);
    d.setWindowTitle(tr("Insertar enlace"));
    auto* form = new QFormLayout(&d);
    auto* label = new QLineEdit(c.selectedText());
    auto* url = new QLineEdit;
    url->setPlaceholderText(QStringLiteral("https://"));
    form->addRow(tr("Texto"), label);
    form->addRow(tr("URL"), url);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    form->addRow(buttons);
    url->setFocus();
    if (d.exec() != QDialog::Accepted || url->text().trimmed().isEmpty()) return;
    const QString title = label->text().trimmed();
    const QString target = url->text().trimmed();
    c.insertText(title.isEmpty() ? QStringLiteral("[%1]").arg(target) : QStringLiteral("[%1|%2]").arg(title, target));
    m_edit->setTextCursor(c);
    m_edit->setFocus();
}

void MarkupEditorDialog::pickColor() {
    const QColor chosen = QColorDialog::getColor(QColor(theme::Red), this, tr("Color del texto"));
    if (!chosen.isValid()) return;
    wrap(QStringLiteral("{color:%1}").arg(chosen.name()), QStringLiteral("{color}"), tr("texto"));
}

void MarkupEditorDialog::updatePreview() {
    jira::MarkupColors colors;
    colors.text = theme::Text;
    colors.border = theme::Border;
    colors.headerBg = theme::Elevated;
    colors.codeBg = theme::Elevated;
    colors.link = theme::Blue;
    const int scroll = m_preview->verticalScrollBar() ? m_preview->verticalScrollBar()->value() : 0;
    m_preview->setHtml(jira::toHtml(m_edit->toPlainText(), colors));
    m_preview->verticalScrollBar()->setValue(scroll);
}

} // namespace qaflow
