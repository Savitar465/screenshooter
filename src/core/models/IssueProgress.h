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

/// Cómo va el control de calidad de un issue: qué se ha ejecutado de sus casos, qué observaciones
/// quedan abiertas y, con eso, si el requerimiento quedaría **Conforme** u **Observado**.
///
/// Es una función pura del issue y de lo que hay en el proyecto (casos, ejecuciones y bugs): no se
/// persiste, se calcula al mostrarlo y al cerrar la revisión. El veredicto es una propuesta: quien
/// cierra la revisión decide, y `blockers` explica por qué se propone eso.
struct IssueProgress {
    int cases = 0;            // casos vinculados que siguen existiendo
    int missingCases = 0;     // ids vinculados de casos ya borrados
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

/// Estado del issue a partir de sus casos, de las ejecuciones que ya trae el historial y del libro de
/// bugs. `runs` son las ejecuciones de los casos del issue (`IssueStore::runsOf`) y `since` acota la
/// revisión en curso: las ejecuciones anteriores son de rondas ya cerradas y no cuentan.
IssueProgress issueProgress(const Issue& issue, const QList<TestCase>& cases, const QList<RunRecord>& runs,
                            const QList<IssueLink>& bugs, const QDateTime& since = QDateTime());

} // namespace qaflow
