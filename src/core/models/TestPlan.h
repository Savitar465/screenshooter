#pragma once

#include <QDateTime>
#include <QList>
#include <QString>
#include <QStringList>

namespace qaflow {

/// Plan de pruebas: un conjunto ordenado de casos que se ejecutan en ciclos.
/// Cada ejecución del plan queda en el historial como un PlanRun que apunta a este id.
struct TestPlan {
    QString id;                // PL-0001
    QString name = QStringLiteral("Regresión Sprint 14");
    QStringList caseIds;       // en orden de ejecución
    bool archived = false;
    QDateTime createdAt;

    bool contains(const QString& id) const { return caseIds.contains(id); }
    int indexOf(const QString& id) const { return caseIds.indexOf(id); }
};

/// Todos los planes y cuál está abierto en la pantalla de planes.
struct PlanCollection {
    QString activeId;
    QList<TestPlan> plans;
};

} // namespace qaflow
