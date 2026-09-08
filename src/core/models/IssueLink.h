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
    QString tracker;         // "Jira", "GitHub", …
    QString severity;
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
