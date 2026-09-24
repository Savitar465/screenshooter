#include "ElidedLabel.h"

#include <QResizeEvent>
#include <QTextLayout>

#include <algorithm>

namespace qaflow {

ElidedLabel::ElidedLabel(const QString& text, QWidget* parent) : QLabel(parent) {
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setFullText(text);
}

void ElidedLabel::setFullText(const QString& text) {
    m_full = text;
    setToolTip(text);
    updateElided();
}

void ElidedLabel::setMaxLines(int lines) {
    m_maxLines = std::max(1, lines);
    setWordWrap(m_maxLines > 1);
    updateElided();
}

QSize ElidedLabel::sizeHint() const {
    if (m_maxLines > 1) return QLabel::sizeHint();
    const QSize hint = QLabel::sizeHint();
    const QFontMetrics fm = fontMetrics();
    return {hint.width() + fm.horizontalAdvance(m_full) - fm.horizontalAdvance(text()), hint.height()};
}

QSize ElidedLabel::minimumSizeHint() const { return {0, QLabel::minimumSizeHint().height()}; }

void ElidedLabel::resizeEvent(QResizeEvent* e) {
    QLabel::resizeEvent(e);
    updateElided();
}

void ElidedLabel::updateElided() {
    // Mientras no tiene ancho (aún sin colocar) se queda con el texto entero: el primer resize lo recorta.
    const int w = contentsRect().width();
    if (w <= 0) {
        setText(m_full);
        return;
    }
    if (m_maxLines == 1) {
        setText(fontMetrics().elidedText(m_full, Qt::ElideRight, w));
        return;
    }
    // Varias líneas: se parte el texto como lo partiría la etiqueta y, si sobran, la última que cabe se
    // queda con todo lo que queda, recortado.
    QTextLayout layout(m_full, font());
    QList<int> starts;
    layout.beginLayout();
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(w);
        starts << line.textStart();
    }
    layout.endLayout();
    QString shown = m_full;
    if (starts.size() > m_maxLines) {
        const int last = starts.at(m_maxLines - 1);
        shown = m_full.left(last) + fontMetrics().elidedText(m_full.mid(last).simplified(), Qt::ElideRight, w);
    }
    if (shown != text()) setText(shown);
}

} // namespace qaflow
