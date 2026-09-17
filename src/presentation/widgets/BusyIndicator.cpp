#include "BusyIndicator.h"

#include "presentation/theme/Theme.h"

#include <QPainter>

#include <algorithm>

namespace qaflow {

namespace {
constexpr int kTickMs = 60;      // ~16 pasos por vuelta: se ve fluido sin repintar de más
constexpr int kStepDeg = 24;
constexpr int kArcDeg = 280;
} // namespace

BusyIndicator::BusyIndicator(int size, QWidget* parent) : QWidget(parent), m_color(theme::Blue) {
    setFixedSize(size, size);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    m_timer.setInterval(kTickMs);
    connect(&m_timer, &QTimer::timeout, this, [this]() {
        m_angle = (m_angle + kStepDeg) % 360;
        update();
    });
}

void BusyIndicator::setColor(const QString& color) {
    m_color = color;
    update();
}

void BusyIndicator::start() {
    if (!m_timer.isActive()) m_timer.start();
    update();
}

void BusyIndicator::stop() {
    m_timer.stop();
    update();
}

void BusyIndicator::showEvent(QShowEvent* e) {
    QWidget::showEvent(e);
    start();
}

void BusyIndicator::hideEvent(QHideEvent* e) {
    QWidget::hideEvent(e);
    m_timer.stop();
}

void BusyIndicator::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal pen = std::max(2.0, width() / 8.0);
    const QRectF ring = QRectF(rect()).adjusted(pen / 2, pen / 2, -pen / 2, -pen / 2);

    // El aro apagado de fondo y, encima, el arco que gira: así se ve dónde está aunque esté parado.
    QColor color(m_color);
    QColor track = color;
    track.setAlpha(50);
    p.setPen(QPen(track, pen));
    p.drawEllipse(ring);
    p.setPen(QPen(color, pen, Qt::SolidLine, Qt::RoundCap));
    // Qt mide en dieciseisavos de grado y en sentido antihorario: el signo hace que gire como un reloj.
    p.drawArc(ring, -m_angle * 16, -kArcDeg * 16);
}

} // namespace qaflow
