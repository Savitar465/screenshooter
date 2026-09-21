#include "ElidedLabel.h"

#include <QResizeEvent>

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

QSize ElidedLabel::sizeHint() const {
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
    setText(w > 0 ? fontMetrics().elidedText(m_full, Qt::ElideRight, w) : m_full);
}

} // namespace qaflow
