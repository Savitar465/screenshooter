#pragma once

#include <QString>
#include <QStringList>

namespace qaflow {

/// Defecto a crear en el gestor de incidencias. Los campos de la parte superior son comunes;
/// los de "Campos del gestor" se mapean a campos reales de Jira / GitHub / GitLab / Azure DevOps.
struct BugReport {
    QString title;
    QString severity = QStringLiteral("Mayor");      // Bloqueante, Crítica, Mayor, Menor, Trivial
    QString environment = QStringLiteral("Staging"); // Staging, QA, Producción
    QString linkedCaseId;
    QString linkedStoryKey;   // historia enlazada al caso (opcional)
    QString stepsToReproduce;
    QString expected;
    QString actual;
    QStringList attachmentPaths;

    // Campos del gestor (vacío = no enviar / valor por defecto del servidor)
    QString issueType = QStringLiteral("Bug");
    QString priority;            // nombre en el gestor: Highest/High/… (Jira), 1-4 (Azure), etiqueta (GitHub/GitLab)
    QString assigneeId;          // accountId (Jira Cloud), login (GitHub), id numérico (GitLab), email (Azure)
    QString assigneeName;        // sólo para mostrar
    QStringList components;      // componentes (Jira) o etiquetas (GitHub/GitLab)
    QStringList affectsVersions; // versiones afectadas (Jira) o milestone (GitHub/GitLab)
    QStringList labels;          // etiquetas adicionales

    bool isValid() const { return !title.trimmed().isEmpty() && !actual.trimmed().isEmpty(); }

    /// Severidades y entornos admitidos (valores canónicos, en el orden del formulario).
    static QStringList severities();
    static QStringList environments();
    /// Texto para mostrar de una severidad o entorno canónicos, en el idioma de la interfaz.
    static QString severityLabel(const QString& severity);
    static QString environmentLabel(const QString& environment);
    /// Prioridad de Jira sugerida a partir de la severidad ("Bloqueante" → "Highest"…).
    static QString jiraPriorityFor(const QString& severity);

    /// Descripción en formato wiki de Jira.
    QString jiraDescription() const;
    /// Descripción en Markdown (GitHub, GitLab). `attachmentLinks` se añaden al final si hay.
    QString markdownDescription(const QStringList& attachmentLinks = {}) const;
    /// Descripción en HTML (Azure DevOps).
    QString htmlDescription() const;
};

} // namespace qaflow
