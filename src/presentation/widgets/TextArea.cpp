#include "TextArea.h"

#include "presentation/widgets/MarkupEditorDialog.h"
#include "presentation/widgets/Ui.h"

#include <QContextMenuEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextLayout>

#include <algorithm>
#include <cmath>

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

void TextArea::setAutoHeight(int minRows, int maxRows) {
    m_minRows = std::max(1, minRows);
    m_maxRows = std::max(m_minRows, maxRows);
    // El alto lo da heightForWidth: así el layout lo reparte en la misma pasada en la que fija el ancho.
    setMinimumHeight(0);
    setMaximumHeight(QWIDGETSIZE_MAX);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    setSizePolicy(policy);
    // Con un método y no una lambda: Qt::UniqueConnection sólo acepta punteros a miembro.
    connect(this, &QPlainTextEdit::textChanged, this, &TextArea::refit, Qt::UniqueConnection);
    fitHeight();
}

void TextArea::setSingleParagraph(bool single) {
    m_single = single;
    if (single && toPlainText().contains(QLatin1Char('\n'))) setPlainText(toPlainText().replace(QLatin1Char('\n'), QLatin1Char(' ')));
}

void TextArea::setMarkupButtonOnFocus(bool onFocus) {
    m_markupOnFocus = onFocus;
    updateMarkupButton();
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
    updateMarkupButton();
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

bool TextArea::viewportEvent(QEvent* event) {
    const bool handled = QPlainTextEdit::viewportEvent(event);
    // Con el viewport colocado se sabe cuánto mide el campo además de él; con otro ancho, además, el
    // texto puede pasar a caber (o dejar de caber) sin desplazarse.
    if (event->type() == QEvent::Resize) fitHeight();
    return handled;
}

void TextArea::keyPressEvent(QKeyEvent* event) {
    if (m_single && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && event->modifiers() == Qt::NoModifier) {
        focusNextChild();
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void TextArea::insertFromMimeData(const QMimeData* source) {
    if (!m_single || !source->hasText()) {
        QPlainTextEdit::insertFromMimeData(source);
        return;
    }
    QString text = source->text();
    text.replace(QStringLiteral("\r\n"), QStringLiteral(" ")).replace(QLatin1Char('\n'), QLatin1Char(' ')).replace(QLatin1Char('\r'), QLatin1Char(' '));
    insertPlainText(text);
}

void TextArea::focusInEvent(QFocusEvent* event) {
    QPlainTextEdit::focusInEvent(event);
    updateMarkupButton();
}

void TextArea::focusOutEvent(QFocusEvent* event) {
    QPlainTextEdit::focusOutEvent(event);
    updateMarkupButton();
}

void TextArea::changeEvent(QEvent* event) {
    QPlainTextEdit::changeEvent(event);
    // La hoja de estilos cambia la letra (y el relleno) después de construirlo.
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange) {
        m_lastHeight = -1;
        fitHeight();
    }
}

bool TextArea::hasHeightForWidth() const { return m_minRows > 0 || QPlainTextEdit::hasHeightForWidth(); }

int TextArea::heightForWidth(int width) const {
    if (m_minRows == 0) return QPlainTextEdit::heightForWidth(width);
    const int line = lineHeight();
    const int text = std::clamp(textHeight(width), line * m_minRows, line * m_maxRows);
    return text + m_chrome.height() + int(std::ceil(2 * document()->documentMargin())) + 2;
}

QSize TextArea::sizeHint() const {
    const QSize base = QPlainTextEdit::sizeHint();
    return m_minRows == 0 ? base : QSize(base.width(), heightForWidth(width() > 0 ? width() : base.width()));
}

QSize TextArea::minimumSizeHint() const {
    const QSize base = QPlainTextEdit::minimumSizeHint();
    return m_minRows == 0 ? base : QSize(base.width(), heightForWidth(width() > 0 ? width() : base.width()));
}

namespace {
/// Lo que ocupa `paragraph` partido en líneas de `width`, con el mismo alto de línea con el que lo
/// coloca QPlainTextEdit (el de cada línea más su interlineado, no el de la métrica de la letra).
qreal layoutHeight(const QString& paragraph, const QFont& font, qreal width) {
    QTextLayout layout(paragraph, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    qreal height = 0;
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(width);
        height += line.height();
        if (line.leadingIncluded()) height += std::max<qreal>(0, line.leading());
    }
    layout.endLayout();
    return height;
}
} // namespace

int TextArea::lineHeight() const { return int(std::ceil(layoutHeight(QStringLiteral("X"), font(), 1000))); }

int TextArea::textHeight(int width) const {
    // El texto (o, vacío, lo que se ve en su lugar) partido en líneas como lo parte el campo.
    const QString text = toPlainText().isEmpty() ? placeholderText() : toPlainText();
    const qreal avail = std::max(20, width - m_chrome.width() - int(std::ceil(2 * document()->documentMargin())) - 1);
    const qreal empty = layoutHeight(QStringLiteral("X"), font(), avail);
    qreal height = 0;
    for (const QString& paragraph : text.split(QLatin1Char('\n')))
        height += paragraph.isEmpty() ? empty : layoutHeight(paragraph, font(), avail);
    return int(std::ceil(height));
}

void TextArea::refit() {
    m_lastHeight = -1;   // otro texto: los layouts tienen que volver a pedir el alto a cualquier ancho
    fitHeight();
}

void TextArea::fitHeight() {
    if (m_minRows == 0) return;
    // Marco, relleno de la hoja de estilos y franja del botón de formato: lo que el campo mide además de
    // su viewport. Sólo se fía de ello con el viewport ya colocado (antes de mostrarse mide cualquier cosa).
    const QSize vp = viewport()->size();
    if (vp.width() > 0 && vp.height() > 0 && vp.width() <= width() && vp.height() <= height()) {
        QSize chrome(width() - vp.width(), height() - vp.height());
        if (verticalScrollBar()->isVisible()) chrome.rwidth() -= verticalScrollBar()->width();
        // Los layouts guardan el alto que dio a otros anchos: si cambia la medida, que lo pidan de nuevo.
        if (chrome != m_chrome) m_lastHeight = -1;
        m_chrome = chrome;
    }
    const bool overflow = textHeight(width()) > lineHeight() * m_maxRows + 1;
    setVerticalScrollBarPolicy(overflow ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
    if (!overflow) verticalScrollBar()->setValue(0);   // si cabe entero, que no quede desplazado
    if (heightForWidth(width()) != m_lastHeight) {
        m_lastHeight = heightForWidth(width());
        updateGeometry();
    }
}

void TextArea::updateMarkupButton() {
    if (m_markupButton) m_markupButton->setVisible(!m_markupOnFocus || hasFocus());
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
