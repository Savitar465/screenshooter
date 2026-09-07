#include "TextArea.h"

#include <QFontMetrics>

namespace qaflow {

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

} // namespace qaflow
