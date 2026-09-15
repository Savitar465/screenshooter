#pragma once

#include "core/models/Issue.h"
#include "core/models/IssueLink.h"
#include "core/models/QualityRecord.h"
#include "core/models/RunHistory.h"
#include "core/models/TestCase.h"

#include <QList>
#include <QString>

namespace qaflow::quality {

/// Lo que el acta necesita y no está en el issue: quién la firma, de qué acta anterior se heredan los
/// datos del entorno y qué observaciones venían de la ronda anterior (las corregidas son las
/// «Cantidad de Correcciones» del formulario).
struct DraftContext {
    QString qaResource;                // recurso de QA (el usuario de la conexión con GESREQ)
    int revisionNumber = 1;
    QualityRecord previous;            // última acta del proyecto; vacía la primera vez
    QList<IssueLink> previousBugs;     // observaciones levantadas en revisiones anteriores
};

/// El acta que QAflow propone para la revisión: rellena lo que sabe del requerimiento, de los casos,
/// de las ejecuciones y de los bugs, hereda del acta anterior lo que no cambia entre requerimientos
/// (servidor, base de datos, departamento, membrete) y deja el resto como lo escribe el formulario
/// («S/D», «n/a»). Todo es corregible después: esto es un punto de partida, no la última palabra.
///
/// `runs` y `bugs` son los de la revisión en curso (`IssueStore::runsOf` filtrado por su comienzo).
QualityRecord draftFor(const Issue& issue, const QList<TestCase>& cases, const QList<RunRecord>& runs,
                       const QList<IssueLink>& bugs, const DraftContext& context);

/// Resumen del resultado en texto plano, el que se manda al gestor y a GESREQ: qué revisión es, cómo
/// quedaron los casos y cuántas observaciones hay por tipo.
QString summaryOf(const QualityRecord& record, QaOutcome outcome, const QList<RunRecord>& runs);

} // namespace qaflow::quality
