#pragma once

#include <QDate>
#include <QList>
#include <QString>
#include <QStringList>

namespace qaflow {

/// Una fila del «Resumen Observaciones» del acta: cuántas observaciones de ese tipo se levantaron y
/// cuántas de las anteriores se dieron por corregidas.
struct ObservationCount {
    QString type;              // "A"… "E" (ver BugReport::classifications)
    int observations = 0;
    int corrections = 0;
};

/// Una fila de la tabla «Resultados»: la característica revisada, si la satisface y su comentario.
struct QualityCharacteristic {
    QString text;
    bool satisfied = true;
    QString note;
};

/// El acta «REVISIÓN CONTROL DE CALIDAD DE SOFTWARE» (R-213) como datos: lo que va en cada celda del
/// formulario, sin nada de Word. QAflow rellena lo que sabe (el requerimiento, los casos, las
/// ejecuciones y los bugs) y el resto se escribe una vez y se hereda del acta anterior del proyecto.
struct QualityRecord {
    // ---- Generales -----------------------------------------------------------------------------
    QString greq;                                          // número del requerimiento (2026997)
    QString process = QStringLiteral("Según Requerimiento");
    QString system;                                        // "SUMA V2 INGRESO"
    QString moduleLink;                                    // repositorio o módulo revisado
    QString server = QStringLiteral("S/D");
    QString dbAccess = QStringLiteral("S/D");
    QString dbSchema = QStringLiteral("S/D");
    QString dbUser = QStringLiteral("S/D");
    QString appUser = QStringLiteral("S/D");
    QString tables = QStringLiteral("n/a");
    QString functions = QStringLiteral("n/a");
    QString description;                                   // alcance del requerimiento
    QString developedBy;
    QString qaResource;                                    // quién hizo el control de calidad
    QString department = QStringLiteral("Departamento de Investigación y Desarrollo de Sistemas");
    int revisionNumber = 1;
    QDate from;                                            // "Fecha de Revisión": 09/09/2026 a 10/09/2026
    QDate to;

    // ---- Resumen de observaciones --------------------------------------------------------------
    QList<ObservationCount> observations = emptyObservations();

    // ---- Detalles de la revisión ---------------------------------------------------------------
    QString caseDesign;            // «Elaboración de Casos de prueba»
    QString execution;             // «Ejecución de casos de pruebas»
    QString bugs;                  // «Bugs Reportados»
    QStringList executionImages;   // capturas que acompañan a la fila de ejecución (rutas en disco)

    // ---- Resultados ----------------------------------------------------------------------------
    QList<QualityCharacteristic> characteristics = defaultCharacteristics();
    QString generalNotes;

    /// Membrete de la cabecera (PNG o JPG en disco); vacío = el acta sale sin logo.
    QString logoPath;

    int totalObservations() const;
    int totalCorrections() const;
    /// Fechas de revisión como las escribe el formulario: "09/09/2026 a 10/09/2026", o una sola.
    QString reviewDates() const;
    bool isEmpty() const { return greq.trimmed().isEmpty() && description.trimmed().isEmpty(); }

    /// Las cinco filas del resumen, en orden y a cero.
    static QList<ObservationCount> emptyObservations();
    /// Las cuatro características impresas en el formulario. No se traducen: son el texto del acta.
    static QList<QualityCharacteristic> defaultCharacteristics();
};

} // namespace qaflow
