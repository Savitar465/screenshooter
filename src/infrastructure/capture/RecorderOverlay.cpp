#include "RecorderOverlay.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>

namespace qaflow {

RecorderOverlay::RecorderOverlay(const QRect& screenGeometry, const QRect& avoid, QWidget* parent)
    : QWidget(parent, Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setFocusPolicy(Qt::StrongFocus);

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(14, 8, 10, 8);
    h->setSpacing(10);
    auto* dot = new QLabel(QStringLiteral("●"));
    dot->setStyleSheet(QStringLiteral("color:#ef4444;font-size:14px;font-weight:800;background:transparent;"));
    h->addWidget(dot);
    m_time = new QLabel(QStringLiteral("00:00"));
    m_time->setStyleSheet(QStringLiteral("color:#e6edf3;font-family:Consolas,'DejaVu Sans Mono',monospace;font-size:13px;font-weight:700;background:transparent;"));
    h->addWidget(m_time);
    m_stop = new QPushButton(QCoreApplication::translate("RecorderOverlay", "Detener"));
    m_stop->setCursor(Qt::PointingHandCursor);
    m_stop->setStyleSheet(QStringLiteral("QPushButton{background:#ef4444;color:#fff;border:none;border-radius:7px;padding:5px 12px;font-weight:700;}"
                                         "QPushButton:hover{background:#dc2626;}"));
    connect(m_stop, &QPushButton::clicked, this, &RecorderOverlay::stopRequested);
    h->addWidget(m_stop);
    auto* hint = new QLabel(QCoreApplication::translate("RecorderOverlay", "Esc cancela"));
    hint->setStyleSheet(QStringLiteral("color:#8b949e;font-size:11px;background:transparent;"));
    h->addWidget(hint);
    adjustSize();

    // Esquina inferior derecha de la pantalla; si la región grabada la tapa, arriba a la derecha.
    const QSize s = sizeHint();
    QPoint pos(screenGeometry.right() - s.width() - 16, screenGeometry.bottom() - s.height() - 16);
    if (avoid.intersects(QRect(pos, s))) pos.setY(screenGeometry.top() + 16);
    if (avoid.intersects(QRect(pos, s))) pos.setX(screenGeometry.left() + 16);
    move(pos);
}

void RecorderOverlay::setElapsed(int secs, int maxSecs) {
    m_time->setText(QStringLiteral("%1:%2 / %3:%4")
                        .arg(secs / 60, 2, 10, QLatin1Char('0')).arg(secs % 60, 2, 10, QLatin1Char('0'))
                        .arg(maxSecs / 60, 2, 10, QLatin1Char('0')).arg(maxSecs % 60, 2, 10, QLatin1Char('0')));
}

void RecorderOverlay::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { emit cancelRequested(); return; }
    if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter || e->key() == Qt::Key_Space) { emit stopRequested(); return; }
    QWidget::keyPressEvent(e);
}

void RecorderOverlay::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(rect().adjusted(0, 0, -1, -1), 10, 10);
    p.fillPath(path, QColor(22, 27, 34, 235));
    p.setPen(QColor(48, 54, 61));
    p.drawPath(path);
}

} // namespace qaflow
