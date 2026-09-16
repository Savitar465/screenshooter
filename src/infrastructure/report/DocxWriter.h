#pragma once

#include <QList>
#include <QString>

namespace qaflow::docx {

enum class Align { Left, Center, Right, Justify };
/// Celda combinada verticalmente: la primera fila abre la combinación y las siguientes la continúan.
enum class VMerge { None, Restart, Continue };

/// Un párrafo: un texto (con sus saltos de línea) o una imagen del disco. Es la unidad con la que se
/// rellenan las celdas, porque una celda del acta puede tener varias líneas y alguna captura.
struct Paragraph {
    QString text;
    bool bold = false;
    Align align = Align::Left;
    int size = 20;                 // medios puntos: 20 = 10 pt, el cuerpo del formulario
    QString color;                 // color del texto en hexadecimal ("FFFFFF"); vacío = el del documento
    QString imagePath;             // si no está vacío, el párrafo es esa imagen
    int imageMaxWidth = 0;         // twips; 0 = el ancho útil de la página

    bool isImage() const { return !imagePath.trimmed().isEmpty(); }
};

struct Cell {
    QList<Paragraph> paragraphs;
    int width = 0;                 // twips
    int gridSpan = 1;
    VMerge vMerge = VMerge::None;
    QString shade;                 // fondo en hexadecimal ("D9D9D9"); vacío = sin fondo
};

struct Row {
    QList<Cell> cells;
};

/// Una tabla con bordes: `grid` son los anchos de sus columnas en twips y las celdas los combinan con
/// `gridSpan`/`vMerge`, que es como está maquetado el formulario R-213.
struct Table {
    QList<int> grid;
    QList<Row> rows;
};

/// Un bloque del documento: una tabla o un párrafo suelto (los que separan las tablas).
struct Block {
    Table table;
    Paragraph paragraph;

    bool isTable() const { return !table.rows.isEmpty(); }
};

struct Result {
    bool ok = false;
    QString error;
};

/// Escribe el documento como .docx (OOXML dentro de un ZIP). Las imágenes que no se puedan leer se
/// omiten en vez de romper el documento; el resto del acta sale igual.
Result write(const QString& path, const QList<Block>& blocks);

/// Ancho útil de una página carta vertical con los márgenes que usa el formulario, en twips.
int pageWidth();

} // namespace qaflow::docx
