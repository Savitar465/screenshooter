#pragma once

#include "core/models/Issue.h"
#include "core/models/IssueLink.h"
#include "core/models/RunHistory.h"
#include "core/models/TestCase.h"

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace qaflow {

/// Cómo va el control de calidad de un issue: qué se ha ejecutado de los casos de sus planes, qué
/// observaciones quedan abiertas y, con eso, si el requerimiento quedaría **Conforme** u **Observado**.
///
/// Es una función pura del issue y de lo que hay en el proyecto (casos, ejecuciones y bugs): no se
/// persiste, se calcula al mostrarlo y al cerrar la revisión. El veredicto es una propuesta: quien
/// cierra la revisión decide, y `blockers` explica por qué se propone eso.
struct IssueProgress {
    int cases = 0;            // casos de sus planes que siguen existiendo
    int missingCases = 0;     // casos de sus planes que ya se borraron del catálogo
    int executed = 0;         // casos con alguna ejecución en esta revisión
    int passed = 0;
    int failed = 0;
    int blocked = 0;
    int bugs = 0;             // bugs reportados desde los casos del issue
    int openBugs = 0;
    QaOutcome suggested = QaOutcome::Pendiente;
    QStringList blockers;     // "2 casos sin ejecutar", "1 bug abierto"

    int notRun() const { return cases - executed; }
    /// Hay algo que registrar: al menos un caso ejecutado en la revisión.
    bool canClose() const { return executed > 0; }
};

/// Estado del issue a partir de los casos que prueban sus planes (`caseIds`, en el orden de los
/// planes), de las ejecuciones que ya trae el historial y del libro de bugs. `runs` son las
/// ejecuciones de los ciclos de esos planes (`IssueStore::runsOf`) y `since` acota la revisión en
/// curso: las ejecuciones anteriores son de rondas ya cerradas y no cuentan.
IssueProgress issueProgress(const QStringList& caseIds, const QList<TestCase>& cases, const QList<RunRecord>& runs,
                            const QList<IssueLink>& bugs, const QDateTime& since = QDateTime());

/// ¿Se volvió a probar lo que rompió este bug, y pasó? Mira la **última** ejecución de su caso posterior
/// al bug (sin contar aquella en la que se encontró): el paso del bug tiene que estar ahí, probado de
/// nuevo —no heredado de la ejecución que se continuaba— y superado; un bug del caso entero (paso 0)
/// pide el caso superado. Es lo que permite dar un bug por corregido y cerrarlo en el gestor.
bool retestPassed(const IssueLink& bug, const QList<RunRecord>& runs);

} // namespace qaflow
