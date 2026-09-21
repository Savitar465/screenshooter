#pragma once

#include <QPixmap>
#include <QString>

/// Iconos de línea del rail de navegación y de las acciones de la ejecución. Se dibujan al vuelo con QPainter (como `ui::appIcon()`)
/// para no depender del plugin SVG ni de ficheros de recursos, y se piden en el color de la paleta
/// activa: el mismo glifo se usa apagado (inactivo) y con el color de la pantalla (activo).
namespace qaflow::icons {

enum class Glyph {
    Cases, Plan, Run, History, Bug, Metrics, Issues, Capture, Focus, Annotate,
    // Herramientas del editor de anotaciones.
    Arrow, Rectangle, Ellipse, Highlight, Text, Blur, Undo, Fit
};

/// Glifo `g` dibujado en `color` sobre un lienzo cuadrado de `size` puntos, listo para HiDPI.
QPixmap pixmap(Glyph g, const QString& color, int size = 22);

} // namespace qaflow::icons
