#include "Icons.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>

namespace qaflow::icons {

namespace {

// Todos los glifos se dibujan sobre una rejilla de 24×24 y `pixmap()` la escala al tamaño pedido.
constexpr qreal kGrid = 24.0;
constexpr qreal kStroke = 1.7;

/// Documento con una marca de verificación: casos de prueba.
void drawCases(QPainter& p) {
    p.drawRoundedRect(QRectF(4, 3.5, 16, 17), 3, 3);
    QPainterPath check;
    check.moveTo(8, 12);
    check.lineTo(10.8, 14.8);
    check.lineTo(16.2, 8.6);
    p.drawPath(check);
}

/// Lista ordenada (viñeta + línea): el plan de pruebas es una secuencia de casos.
void drawPlan(QPainter& p) {
    p.setBrush(p.pen().color());
    for (const qreal y : {7.5, 12.0, 16.5}) {
        p.drawEllipse(QPointF(6, y), 1.4, 1.4);
        p.drawLine(QPointF(10.5, y), QPointF(19, y));
    }
    p.setBrush(Qt::NoBrush);
}

/// Triángulo de reproducción: ejecución en curso.
void drawRun(QPainter& p) {
    QPainterPath t;
    t.moveTo(8.5, 5.5);
    t.lineTo(18.5, 12);
    t.lineTo(8.5, 18.5);
    t.closeSubpath();
    p.fillPath(t, p.pen().color());
}

/// Reloj: el historial de ejecuciones.
void drawHistory(QPainter& p) {
    p.drawEllipse(QPointF(12, 12), 8, 8);
    p.drawLine(QPointF(12, 12), QPointF(12, 7.2));
    p.drawLine(QPointF(12, 12), QPointF(15.6, 13.6));
}

/// Bicho: el reporte de bugs.
void drawBug(QPainter& p) {
    p.drawRoundedRect(QRectF(8, 8, 8, 11), 4, 4);
    p.drawLine(QPointF(9.6, 8), QPointF(8, 5));       // antenas
    p.drawLine(QPointF(14.4, 8), QPointF(16, 5));
    for (const qreal y : {10.5, 14.0, 17.0}) {        // patas
        p.drawLine(QPointF(8, y), QPointF(4.8, y - 1));
        p.drawLine(QPointF(16, y), QPointF(19.2, y - 1));
    }
}

/// Barras ascendentes: las métricas.
void drawMetrics(QPainter& p) {
    QPen pen = p.pen();
    pen.setWidthF(2.6);
    p.setPen(pen);
    p.drawLine(QPointF(6.5, 18), QPointF(6.5, 13));
    p.drawLine(QPointF(12, 18), QPointF(12, 9.5));
    p.drawLine(QPointF(17.5, 18), QPointF(17.5, 6));
}

} // namespace

QPixmap pixmap(Glyph g, const QString& color, int size) {
    const qreal dpr = qGuiApp ? qGuiApp->devicePixelRatio() : 1.0;
    QPixmap pm(QSize(size, size) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);

    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / kGrid, size / kGrid);
    p.setPen(QPen(QColor(color), kStroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    switch (g) {
        case Glyph::Cases: drawCases(p); break;
        case Glyph::Plan: drawPlan(p); break;
        case Glyph::Run: drawRun(p); break;
        case Glyph::History: drawHistory(p); break;
        case Glyph::Bug: drawBug(p); break;
        case Glyph::Metrics: drawMetrics(p); break;
    }
    return pm;
}

} // namespace qaflow::icons
