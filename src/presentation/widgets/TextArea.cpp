#include "TextArea.h"

#include "presentation/widgets/MarkupEditorDialog.h"
#include "presentation/widgets/Ui.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QMenu>
#include <QTextCursor>

namespace qaflow {

namespace {
constexpr int kButtonSize = 20;
}

TextArea::TextArea(int rows, QWidget* parent) : QPlainTextEdit(parent) {
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setLineWrapMode(QPlainTextEdit::WidgetWidth);
    setTabChangesFocus(true);
    const int lineH = QFontMetrics(font()).lineSpacing();
    setFixedHeight(lineH * rows + 22);
    connect(this, &QPlainTextEdit::textChanged, this, [this]() { if (!m_silent) emit edited(toPlainText()); });
}

void TextArea::setTextSilently(const QString& text) {
    if (toPlainText() == text) return;
    m_silent = true;
    setPlainText(text);
    m_silent = false;
}

void TextArea::enableMarkupEditor(const QString& title) {
    m_markupTitle = title;
    if (m_markupButton) return;
    m_markupButton = ui::button(QStringLiteral("✎"), "icon-move", this);
    m_markupButton->setObjectName(QStringLiteral("markupButton"));
    m_markupButton->setToolTip(tr("Editar con formato: negrita, tablas, listas… (lo que acepta Jira / Zephyr)"));
    m_markupButton->setFocusPolicy(Qt::NoFocus);
    m_markupButton->setFixedSize(kButtonSize, kButtonSize);
    m_markupButton->setStyleSheet(QStringLiteral("padding:0;font-size:11px;"));
    connect(m_markupButton, &QPushButton::clicked, this, &TextArea::openMarkupEditor);
    // El botón va en una franja propia a la derecha, para no tapar el texto.
    setViewportMargins(0, 0, kButtonSize + 4, 0);
    placeMarkupButton();
}

void TextArea::openMarkupEditor() {
    MarkupEditorDialog dialog(m_markupTitle.isEmpty() ? tr("Editar con formato") : m_markupTitle, toPlainText(), this);
    if (dialog.exec() != QDialog::Accepted || dialog.text() == toPlainText()) return;
    // Con un cursor, y no con setPlainText, para que Ctrl+Z en el campo deshaga el cambio.
    QTextCursor c(document());
    c.select(QTextCursor::Document);
    c.insertText(dialog.text());
}

void TextArea::resizeEvent(QResizeEvent* event) {
    QPlainTextEdit::resizeEvent(event);
    placeMarkupButton();
}

void TextArea::contextMenuEvent(QContextMenuEvent* event) {
    QMenu* menu = createStandardContextMenu();
    if (m_markupButton) {
        menu->addSeparator();
        menu->addAction(tr("Editar con formato…"), this, &TextArea::openMarkupEditor);
    }
    menu->exec(event->globalPos());
    delete menu;
}

void TextArea::placeMarkupButton() {
    if (!m_markupButton) return;
    const QRect vp = viewport()->geometry();
    m_markupButton->move(vp.right() + 3, vp.top() + 2);
}

} // namespace qaflow
