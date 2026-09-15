#pragma once

#include <QString>
#include <QStringList>

namespace qaflow {

/// Defecto a crear en el gestor de incidencias. Los campos de la parte superior son comunes;
/// los de "Campos del gestor" se mapean a campos reales de Jira / GitHub / GitLab / Azure DevOps.
struct BugReport {
    QString title;
    QString severity = QStringLiteral("Mayor");      // Bloqueante, Crítica, Mayor, Menor, Trivial
    /// Clasificación del formulario de control de calidad (R-213): A Funcionamiento/Lógica, B Datos,
    /// C Estético/Forma, D Recomendaciones, E Vulnerabilidades. Es lo que cuenta el acta por tipo.
    QString classification = QStringLiteral("A");
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
    /// Tipos de observación del acta, en el orden del formulario: "A"… "E".
    static QStringList classifications();
    /// Nombre del tipo tal y como está impreso en el formulario ("Funcionamiento/Lógica"). No se
    /// traduce: es el texto del acta, que siempre sale en español.
    static QString classificationName(const QString& classification);
    /// Texto para mostrar de una severidad o entorno canónicos, en el idioma de la interfaz.
    static QString severityLabel(const QString& severity);
    static QString environmentLabel(const QString& environment);
    /// "A · Funcionamiento/Lógica", para los combos de la interfaz.
    static QString classificationLabel(const QString& classification);
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
