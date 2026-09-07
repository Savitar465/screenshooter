#include "RegionSelector.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>

namespace qaflow {

RegionSelector::RegionSelector(const QPixmap& background, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool), m_background(background) {
    setAttribute(Qt::WA_DeleteOnClose);
    setCursor(Qt::CrossCursor);
    setMouseTracking(true);
}

QRect RegionSelector::selection() const { return QRect(m_origin, m_current).normalized(); }

void RegionSelector::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.drawPixmap(rect(), m_background);
    p.fillRect(rect(), QColor(14, 17, 22, 150));
    if (m_dragging) {
        const QRect sel = selection();
        p.drawPixmap(sel, m_background, QRect(sel.topLeft() * m_background.devicePixelRatio(), sel.size() * m_background.devicePixelRatio()));
        p.setPen(QPen(QColor(0x6e, 0xa8, 0xfe), 2));
        p.drawRect(sel);
        p.setPen(Qt::white);
        p.drawText(sel.bottomLeft() + QPoint(4, 18), QStringLiteral("%1 × %2").arg(sel.width()).arg(sel.height()));
    } else {
        p.setPen(QColor(0xe6, 0xed, 0xf3));
        QFont f = p.font(); f.setPointSize(14); f.setBold(true); p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral("Arrastra para seleccionar la región · Esc para cancelar"));
    }
}

void RegionSelector::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    m_origin = m_current = e->pos();
    m_dragging = true;
    update();
}

void RegionSelector::mouseMoveEvent(QMouseEvent* e) {
    if (!m_dragging) return;
    m_current = e->pos();
    update();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !m_dragging) return;
    m_current = e->pos();
    m_dragging = false;
    const QRect sel = selection();
    if (sel.width() < 4 || sel.height() < 4) { emit cancelled(); close(); return; }
    emit regionSelected(sel);
    close();
}

void RegionSelector::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { emit cancelled(); close(); }
}

} // namespace qaflow
