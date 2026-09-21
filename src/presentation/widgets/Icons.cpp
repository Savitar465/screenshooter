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

/// Portapapeles con renglones: los issues, el trabajo de QA de cada requerimiento.
void drawIssues(QPainter& p) {
    p.drawRoundedRect(QRectF(5, 4.5, 14, 16), 2.5, 2.5);
    p.drawRoundedRect(QRectF(9, 3, 6, 3.2), 1.2, 1.2);   // pinza
    for (const qreal y : {10.5, 14.0, 17.2}) p.drawLine(QPointF(8.5, y), QPointF(15.5, y));
}

/// Esquinas de un encuadre con un punto: capturar la pantalla.
void drawCapture(QPainter& p) {
    QPainterPath c;
    c.moveTo(4, 8.5); c.lineTo(4, 6); c.quadTo(4, 4, 6, 4); c.lineTo(8.5, 4);
    c.moveTo(15.5, 4); c.lineTo(18, 4); c.quadTo(20, 4, 20, 6); c.lineTo(20, 8.5);
    c.moveTo(20, 15.5); c.lineTo(20, 18); c.quadTo(20, 20, 18, 20); c.lineTo(15.5, 20);
    c.moveTo(8.5, 20); c.lineTo(6, 20); c.quadTo(4, 20, 4, 18); c.lineTo(4, 15.5);
    p.drawPath(c);
    p.setBrush(p.pen().color());
    p.drawEllipse(QPointF(12, 12), 2.2, 2.2);
    p.setBrush(Qt::NoBrush);
}

/// Dos flechas hacia las esquinas: el modo foco, la evidencia a toda la ventana.
void drawFocus(QPainter& p) {
    p.drawLine(QPointF(14, 10), QPointF(19.5, 4.5));
    p.drawLine(QPointF(14.5, 4.5), QPointF(19.5, 4.5));
    p.drawLine(QPointF(19.5, 4.5), QPointF(19.5, 9.5));
    p.drawLine(QPointF(10, 14), QPointF(4.5, 19.5));
    p.drawLine(QPointF(4.5, 14.5), QPointF(4.5, 19.5));
    p.drawLine(QPointF(4.5, 19.5), QPointF(9.5, 19.5));
}

/// Lápiz: anotar la evidencia.
void drawAnnotate(QPainter& p) {
    QPainterPath pen;
    pen.moveTo(15.5, 4.5);
    pen.lineTo(19.5, 8.5);
    pen.lineTo(8.5, 19.5);
    pen.lineTo(4.5, 19.5);
    pen.lineTo(4.5, 15.5);
    pen.closeSubpath();
    p.drawPath(pen);
    p.drawLine(QPointF(13, 7), QPointF(17, 11));
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
        case Glyph::Issues: drawIssues(p); break;
        case Glyph::Capture: drawCapture(p); break;
        case Glyph::Focus: drawFocus(p); break;
        case Glyph::Annotate: drawAnnotate(p); break;
    }
    return pm;
}

} // namespace qaflow::icons
