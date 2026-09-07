#include "ProgressCells.h"

#include <QPainter>
#include <QPainterPath>

namespace qaflow {

ProgressCells::ProgressCells(QWidget* parent) : QWidget(parent) { setFixedHeight(6); }

void ProgressCells::setColors(const QStringList& colors) { m_colors = colors; update(); }

void ProgressCells::paintEvent(QPaintEvent*) {
    if (m_colors.isEmpty()) return;
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const int gap = 3;
    const qreal w = (width() - gap * (m_colors.size() - 1)) / static_cast<qreal>(m_colors.size());
    for (int i = 0; i < m_colors.size(); ++i) {
        QPainterPath path;
        path.addRoundedRect(QRectF(i * (w + gap), 0, w, height()), 3, 3);
        p.fillPath(path, QColor(m_colors[i]));
    }
}

} // namespace qaflow
