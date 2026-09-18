#pragma once

#include "core/models/BugReport.h"

#include <QDateTime>
#include <QList>
#include <QString>

namespace qaflow {

/// Issue ya creado en el gestor, enlazado al caso desde el que se reportó.
struct IssueLink {
    QString key;             // SHOP-143, #12, 4711
    QString url;
    QString title;
    QString caseId;          // caso desde el que se reportó (puede ya no existir)
    /// Ejecución en la que se encontró (`RunRecord::id`), y el ciclo de plan del que era parte
    /// (`PlanRun::id`; vacío si el caso se ejecutaba suelto). Es lo que ata el bug a unas pruebas
    /// concretas: los resultados de esa ejecución enseñan lo que salió de ella. Vacíos en los bugs
    /// anteriores a que se anotara y en los traídos del gestor, que se atribuyen por fecha.
    QString runId;
    QString planRunId;
    /// Paso del caso en el que se vio (1..N); 0 = del caso entero. Con él, publicar la ejecución cuelga
    /// el defecto del paso que falló, y no sólo del caso.
    int step = 0;
    QString tracker;         // "Jira", "GitHub", …
    /// Tipo de incidencia del gestor: "Error", "Mejora"… Vacío en los bugs anteriores a que se
    /// guardara. Es lo que distingue en la lista un error de una mejora.
    QString issueType;
    QString severity;
    /// Tipo de observación del acta (A–E) con el que se reportó; los bugs guardados antes de que
    /// existiera cuentan como "A".
    QString classification = QStringLiteral("A");
    QString status;          // último estado conocido ("Open", "Done", …); vacío = nunca consultado
    bool resolved = false;   // el gestor lo considera cerrado
    QDateTime createdAt;
    QDateTime statusCheckedAt;
};

/// Bug que no se pudo enviar (sin red, servidor caído) y espera un reintento.
struct PendingBug {
    QString id;              // Q-0001
    BugReport report;
    QDateTime createdAt;
    QString lastError;
    int attempts = 0;
};

/// Libro de bugs: los reportados y los que esperan envío. Un único agregado para persistir.
struct BugLedger {
    QList<IssueLink> issues;
    QList<PendingBug> pending;
};

} // namespace qaflow
