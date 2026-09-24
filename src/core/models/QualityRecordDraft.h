#pragma once

#include "core/models/Issue.h"
#include "core/models/IssueLink.h"
#include "core/models/PlanReport.h"
#include "core/models/QualityRecord.h"
#include "core/models/RunHistory.h"
#include "core/models/TestCase.h"

#include <QHash>
#include <QList>
#include <QString>

namespace qaflow::quality {

/// Lo que el acta necesita y no está en el issue: quién la firma, de qué acta anterior se heredan los
/// datos del entorno, qué observaciones venían de la ronda anterior (las corregidas son las
/// «Cantidad de Correcciones» del formulario) y dónde quedaron en Zephyr los ciclos que se publicaron.
struct DraftContext {
    QString qaResource;                // recurso de QA (el usuario de la conexión con GESREQ)
    int revisionNumber = 1;
    /// Fase de la ronda ("QA", "PRE") y las del proyecto: el resumen dice en qué fase se probó y si un
    /// Conforme aprueba esa fase o cierra el control. Sin fase, el resumen no la menciona.
    QString phase;
    QStringList phases;
    /// El acta es la del cierre del control (Conforme en la última fase): su resumen de observaciones
    /// cuenta **todo lo encontrado** en el requerimiento, de todas las rondas y fases, y como correcciones
    /// los bugs ya cerrados. Si no, las observaciones son las de esta ronda y las correcciones, las
    /// cerradas de rondas anteriores.
    bool closesControl = false;
    QualityRecord previous;            // última acta del proyecto; vacía la primera vez
    QList<IssueLink> previousBugs;     // observaciones levantadas en revisiones anteriores
    /// Enlace al ciclo de Zephyr de cada ciclo de plan publicado (`PlanRun::id` → URL).
    QHash<QString, QString> cycleUrls;
};

/// El acta que QAflow propone para la revisión: rellena lo que sabe del requerimiento (la ficha de
/// GESREQ, no sólo la fila de la bandeja), de los ciclos de plan que se probaron y de los bugs,
/// hereda del acta anterior lo que no cambia entre requerimientos (servidor, base de datos,
/// departamento, membrete) y deja el resto como lo escribe el formulario («S/D», «n/a»). Todo es
/// corregible después: esto es un punto de partida, no la última palabra.
///
/// `cycles` son las ejecuciones de plan con las que se levanta el acta (`QualityRecordService::
/// cyclesFor`, normalmente la elegida): de ellas salen las fechas de revisión, los casos con su Test
/// de Zephyr y el detalle de la ejecución. `bugs` son los de la revisión.
QualityRecord draftFor(const Issue& issue, const QList<TestCase>& cases, const QList<PlanReport>& cycles,
                       const QList<IssueLink>& bugs, const DraftContext& context);

/// Resumen del resultado en texto plano, el que se manda al gestor y a GESREQ: qué revisión es, cómo
/// quedaron los casos, dónde están sus pruebas en Zephyr y cuántas observaciones hay por tipo.
QString summaryOf(const QualityRecord& record, QaOutcome outcome, const QList<PlanReport>& cycles,
                  const DraftContext& context = {});

} // namespace qaflow::quality
