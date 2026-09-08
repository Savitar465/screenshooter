#pragma once

#include "core/models/Settings.h"

#include <QColor>
#include <QString>

/// Paleta de la interfaz. Hay dos (oscura, la del diseño de referencia, y clara); la activa se
/// fija con `apply()` antes de construir las vistas, que leen los colores de los nombres de abajo.
/// `resources/styles/app.qss` usa los mismos nombres como tokens (`@bg`, `@tint(green,30)`…)
/// y `stylesheet()` los sustituye por la paleta activa.
namespace qaflow::theme {

struct Palette {
    QString bg, panel, elevated, field, border;
    QString text, textSoft, muted, disabled, onAccent;
    QString blue, blueHover, green, greenHover, red, redHover, redSoft, amber, amberSoft, cyan, violet;
    QString gradientTop, scrollHover, panelTranslucent;
};

const Palette& darkPalette();
const Palette& lightPalette();
/// Paleta que corresponde a un ajuste (`System` consulta al sistema).
const Palette& paletteFor(AppTheme t);
bool systemPrefersDark();

/// Fija la paleta activa. Las vistas ya construidas no cambian: hay que reconstruirlas.
void apply(const Palette& p);
const Palette& current();
bool isDark();

/// app.qss con los tokens sustituidos por la paleta activa.
QString stylesheet();
/// "rgba(r,g,b,alpha)" a partir de un color de la paleta (alpha 0-255).
QString tint(const QString& color, int alpha);

// Colores de la paleta activa, por nombre corto (las vistas los usan en estilos dependientes de datos).
inline QString Bg, Panel, Elevated, Field, Border, Text, TextSoft, Muted, Disabled, OnAccent;
inline QString Blue, Green, Red, RedSoft, Amber, AmberSoft, Cyan, Violet;

struct Pill { QString bg; QString fg; };

/// Colores de la etiqueta de prioridad (valor canónico: Alta / Media / Baja).
Pill priorityPill(const QString& priority);

} // namespace qaflow::theme
