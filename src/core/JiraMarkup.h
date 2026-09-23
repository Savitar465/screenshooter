#pragma once

#include <QString>

namespace qaflow::jira {

/// Colores para enseñar el marcado: los de la paleta activa, que el núcleo no conoce.
struct MarkupColors {
    QString text = QStringLiteral("#1f2328");
    QString border = QStringLiteral("#d0d7de");
    QString headerBg = QStringLiteral("#f0f2f5");
    QString codeBg = QStringLiteral("#f0f2f5");
    QString link = QStringLiteral("#2f6feb");
};

/// Pasa a HTML el marcado wiki de Jira, que es el que guardan los pasos y las precondiciones y el que
/// Zephyr enseña con formato: *negrita*, _cursiva_, +subrayado+, -tachado-, {{monoespaciado}},
/// {color:red}texto{color}, [texto|url], tablas (||cabecera|| y |celda|), listas (* y #, anidadas),
/// títulos (h1.–h6.), citas (bq.), bloques {code}/{noformat}, líneas (----) y saltos (\\).
///
/// Es sólo para la vista previa: el texto que se guarda y se publica es el marcado, tal cual. Lo que
/// no reconoce lo deja como texto, escapado.
QString toHtml(const QString& markup, const MarkupColors& colors = {});

/// Tabla en marcado de Jira a partir de texto separado por tabuladores (lo que se copia de una hoja
/// de cálculo). La primera fila es la cabecera si `header`. Las celdas vacías llevan un espacio,
/// porque Jira junta las barras seguidas; las barras dentro de una celda se escapan.
QString tableFromTsv(const QString& tsv, bool header = true);

/// Tabla vacía de `rows` filas de datos y `cols` columnas, con cabecera.
QString emptyTable(int rows, int cols);

} // namespace qaflow::jira
