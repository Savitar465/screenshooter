#pragma once

#include <QString>
#include <QStringList>

namespace qaflow {

struct BugReport {
    QString title;
    QString severity = QStringLiteral("Mayor");   // Bloqueante, Crítica, Mayor, Menor, Trivial
    QString environment = QStringLiteral("Staging"); // Staging, QA, Producción
    QString linkedCaseId;
    QString stepsToReproduce;
    QString expected;
    QString actual;
    QStringList attachmentPaths;

    bool isValid() const { return !title.trimmed().isEmpty() && !actual.trimmed().isEmpty(); }

    /// Descripción en formato wiki de Jira.
    QString jiraDescription() const;
};

} // namespace qaflow
