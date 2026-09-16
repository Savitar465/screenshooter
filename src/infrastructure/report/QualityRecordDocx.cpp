#include "QualityRecordDocx.h"

#include "core/models/BugReport.h"
#include "infrastructure/report/DocxWriter.h"

#include <QList>

namespace qaflow {

namespace {

using namespace docx;

// La rejilla del formulario: diez columnas cuyas combinaciones dan todas las filas de «Generales» y
// del «Resumen Observaciones». Son las del R-213 llevadas al ancho útil de la página (docx::pageWidth).
const QList<int> kGrid{735, 1722, 516, 1835, 28, 856, 600, 600, 600, 1346};
/// Fondo de las cabeceras de sección del formulario (azul oscuro, con el texto en blanco).
const QString kSectionShade = QStringLiteral("1F4E79");
const QString kSectionText = QStringLiteral("FFFFFF");
/// Fondo de las etiquetas y de las cabeceras de columna (azul claro).
const QString kShade = QStringLiteral("DEEAF6");

Paragraph text(const QString& value, bool bold = false, Align align = Align::Left, const QString& color = QString()) {
    Paragraph p;
    p.text = value;
    p.bold = bold;
    p.align = align;
    p.color = color;
    return p;
}

Cell cell(const QString& value, int gridSpan = 1, bool bold = false, Align align = Align::Left, const QString& shade = QString(),
          const QString& color = QString()) {
    Cell c;
    c.paragraphs << text(value, bold, align, color);
    c.gridSpan = gridSpan;
    c.shade = shade;
    return c;
}

/// Celda de etiqueta: el nombre del campo, en negrita y sobre el fondo claro del formulario.
Cell labelCell(const QString& value, int gridSpan = 1) { return cell(value, gridSpan, true, Align::Left, kShade); }

/// Celda de una fila que ocupa toda la tabla: los títulos de sección («1. Generales», «4. Resultados»…),
/// numerados y en blanco sobre el azul del formulario.
Row sectionRow(int number, const QString& title, int columns) {
    Row row;
    row.cells << cell(QStringLiteral("%1.  %2").arg(number).arg(title), columns, true, Align::Left, kSectionShade, kSectionText);
    return row;
}

/// Fila «etiqueta | valor» de los generales. La etiqueta va pegada al valor, como en el formulario.
Row labelRow(const QString& label, const QString& value) {
    Row row;
    Cell name = labelCell(label, 2);
    name.paragraphs.first().align = Align::Right;
    row.cells << name << cell(value, 8);
    return row;
}

/// Fila del bloque «Sistema/Aplicación/Módulo», que comparte la primera celda combinada.
Row systemRow(const QString& label, const QString& value, VMerge merge) {
    Cell first;
    first.gridSpan = 2;
    first.vMerge = merge;
    first.shade = kShade;
    if (merge == VMerge::Restart) first.paragraphs << text(QStringLiteral("Sistema/ Aplicación/Módulo"), true);
    Cell name = labelCell(label, 2);
    name.paragraphs.first().align = Align::Right;
    Row row;
    row.cells << first << name << cell(value, 6);
    return row;
}

Table generalTable(const QualityRecord& r) {
    Table table;
    table.grid = kGrid;

    // Cabecera: membrete, nombre del registro y los datos del formulario.
    Cell logo;
    logo.gridSpan = 3;
    logo.vMerge = VMerge::Restart;
    if (!r.logoPath.trimmed().isEmpty()) {
        Paragraph image;
        image.imagePath = r.logoPath;
        image.imageMaxWidth = 2800;
        image.align = Align::Center;
        logo.paragraphs << image;
    }
    Cell title;
    title.gridSpan = 3;
    title.vMerge = VMerge::Restart;
    title.paragraphs << text(QStringLiteral("REGISTRO"), true, Align::Center)
                     << text(QStringLiteral("REVISIÓN CONTROL DE CALIDAD DE SOFTWARE"), true, Align::Center);
    Row header;
    header.cells << logo << title << cell(QStringLiteral("Código"), 4, true, Align::Center, kShade);
    table.rows << header;

    auto continued = [](int span) {
        Cell c;
        c.gridSpan = span;
        c.vMerge = VMerge::Continue;
        return c;
    };
    Row code;
    code.cells << continued(3) << continued(3) << cell(QStringLiteral("R-213"), 4, false, Align::Center);
    table.rows << code;

    Row versionHeader;
    versionHeader.cells << continued(3) << continued(3) << cell(QStringLiteral("Versión"), 2, true, Align::Center, kShade)
                        << cell(QStringLiteral("Página"), 2, true, Align::Center, kShade);
    table.rows << versionHeader;

    Row version;
    version.cells << continued(3) << continued(3) << cell(QStringLiteral("2"), 2, false, Align::Center)
                  << cell(QStringLiteral("Página 1 de 1"), 2, false, Align::Center);
    table.rows << version;

    table.rows << sectionRow(1, QStringLiteral("Generales"), kGrid.size());

    // Como en el formulario: el número del requerimiento destacado y debajo, el proceso que se revisa.
    Cell process;
    process.gridSpan = 8;
    process.paragraphs << text(QStringLiteral("GREQ %1").arg(r.greq), true) << text(r.process);
    Row processRow;
    processRow.cells << labelCell(QStringLiteral("Proceso en revisión"), 2) << process;
    table.rows << processRow;

    table.rows << systemRow(QStringLiteral("Sistema:"), r.system, VMerge::Restart);
    table.rows << systemRow(QStringLiteral("Enlace/modulo:"), r.moduleLink, VMerge::Continue);
    table.rows << systemRow(QStringLiteral("Servidor:"), r.server, VMerge::Continue);
    table.rows << systemRow(QStringLiteral("Acceso a la BD:"), r.dbAccess, VMerge::Continue);
    table.rows << systemRow(QStringLiteral("Esquema BD:"), r.dbSchema, VMerge::Continue);
    table.rows << systemRow(QStringLiteral("Usuario de BD:"), r.dbUser, VMerge::Continue);
    table.rows << systemRow(QStringLiteral("Usuario Aplicación:"), r.appUser, VMerge::Continue);
    table.rows << systemRow(QStringLiteral("Tablas afectadas:"), r.tables, VMerge::Continue);
    table.rows << systemRow(QStringLiteral("Funciones Afectadas:"), r.functions, VMerge::Continue);
    table.rows << systemRow(QStringLiteral("Descripción con detalle:"), r.description, VMerge::Continue);

    table.rows << labelRow(QStringLiteral("Desarrollado por"), r.developedBy);
    table.rows << labelRow(QStringLiteral("Recurso(s) QA"), r.qaResource);
    table.rows << labelRow(QStringLiteral("Departamento o Institución"), r.department);

    Row revision;
    revision.cells << labelCell(QStringLiteral("Número de Revisión"), 2)
                   << cell(QString::number(r.revisionNumber), 2, false, Align::Center)
                   << labelCell(QStringLiteral("Fecha de Revisión"), 3)
                   << cell(r.reviewDates(), 3, false, Align::Center);
    table.rows << revision;

    table.rows << sectionRow(2, QStringLiteral("Resumen Observaciones"), kGrid.size());

    Row summaryHeader;
    summaryHeader.cells << cell(QStringLiteral("Tipo"), 1, true, Align::Center, kShade)
                        << cell(QStringLiteral("Clasificación de Observaciones"), 4, true, Align::Center, kShade)
                        << cell(QStringLiteral("Cantidad de\nObservaciones"), 4, true, Align::Center, kShade)
                        << cell(QStringLiteral("Cantidad de\nCorrecciones"), 1, true, Align::Center, kShade);
    table.rows << summaryHeader;

    for (const auto& observation : r.observations) {
        Row row;
        row.cells << cell(observation.type, 1, true, Align::Center)
                  << cell(BugReport::classificationName(observation.type), 4)
                  << cell(QString::number(observation.observations), 4, false, Align::Center)
                  << cell(QString::number(observation.corrections), 1, false, Align::Center);
        table.rows << row;
    }

    Row total;
    total.cells << cell(QStringLiteral("Total"), 5, true, Align::Right)
                << cell(QString::number(r.totalObservations()), 4, true, Align::Center)
                << cell(QString::number(r.totalCorrections()), 1, true, Align::Center);
    table.rows << total;
    return table;
}

Table detailTable(const QualityRecord& r) {
    Table table;
    table.grid = {444, 2121, 6273};
    table.rows << sectionRow(3, QStringLiteral("Detalles de la revisión"), table.grid.size());

    Row header;
    header.cells << cell(QStringLiteral("N°"), 1, true, Align::Center, kShade)
                 << cell(QStringLiteral("Opción"), 1, true, Align::Center, kShade)
                 << cell(QStringLiteral("Observación o comentario"), 1, true, Align::Center, kShade);
    table.rows << header;

    auto detailRow = [](int number, const QString& option, const QString& value, const QStringList& images = {}) {
        Cell comment;
        comment.paragraphs << text(value);
        for (const auto& path : images) {
            Paragraph image;
            image.imagePath = path;
            image.imageMaxWidth = 6200;
            comment.paragraphs << image;
        }
        Row row;
        row.cells << cell(QString::number(number), 1, false, Align::Center) << labelCell(option) << comment;
        return row;
    };
    table.rows << detailRow(1, QStringLiteral("Elaboración de Casos de prueba"), r.caseDesign);
    table.rows << detailRow(2, QStringLiteral("Ejecución de casos de pruebas"), r.execution, r.executionImages);
    table.rows << detailRow(3, QStringLiteral("Bugs Reportados"), r.bugs);
    return table;
}

Table resultTable(const QualityRecord& r) {
    Table table;
    table.grid = {3841, 1246, 3751};
    table.rows << sectionRow(4, QStringLiteral("Resultados"), table.grid.size());

    Row header;
    header.cells << cell(QStringLiteral("Característica"), 1, true, Align::Center, kShade)
                 << cell(QStringLiteral("Satisface"), 1, true, Align::Center, kShade)
                 << cell(QStringLiteral("Observaciones"), 1, true, Align::Center, kShade);
    table.rows << header;

    int number = 0;
    for (const auto& characteristic : r.characteristics) {
        Row row;
        row.cells << cell(QStringLiteral("%1.  %2").arg(++number).arg(characteristic.text))
                  << cell(characteristic.satisfied ? QStringLiteral("Si") : QStringLiteral("No"), 1, false, Align::Center)
                  << cell(characteristic.note);
        table.rows << row;
    }

    table.rows << sectionRow(5, QStringLiteral("Observaciones Generales"), table.grid.size());
    Row notes;
    notes.cells << cell(r.generalNotes, table.grid.size());
    table.rows << notes;
    return table;
}

} // namespace

QualityRecordWriteResult QualityRecordDocx::write(const QualityRecord& record, const QString& path) {
    QList<Block> blocks;
    Block general;
    general.table = generalTable(record);
    blocks << general;
    Block separator;
    blocks << separator;
    Block detail;
    detail.table = detailTable(record);
    blocks << detail;
    blocks << separator;
    Block results;
    results.table = resultTable(record);
    blocks << results;

    const docx::Result result = docx::write(path, blocks);
    return {result.ok, result.error};
}

} // namespace qaflow
