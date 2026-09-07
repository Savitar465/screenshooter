#pragma once

#include <QString>
#include <QStringList>

namespace qaflow {

struct TestPlan {
    QString name = QStringLiteral("Regresión Sprint 14");
    QStringList caseIds;

    bool contains(const QString& id) const { return caseIds.contains(id); }
};

} // namespace qaflow
