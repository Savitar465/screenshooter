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
    /// Paso del caso en el que se vio el fallo (1..N); 0 = del caso entero. Es lo que permite colgar el
    /// defecto del paso que le corresponde al publicar la ejecución en Zephyr.
    int linkedStep = 0;
    QString linkedStoryKey;   // historia enlazada al caso (opcional)
    /// Ejecución desde la que se reporta y su ciclo de plan: con ellos el bug queda colgado de unas
    /// pruebas concretas y no del caso en abstracto. Vacíos si no hay ninguna ejecución en curso.
    QString linkedRunId;
    QString linkedPlanRunId;
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
    /// Tipos de incidencia de Jira con los que QAflow trabaja: lo que está mal («Error») y lo que se
    /// pide cambiar («Mejora»). Son los que se proponen al reportar y los que se traen del gestor,
    /// así que lo que QAflow crea es siempre lo que vuelve. Nombres de la instancia: no se traducen.
    static QStringList jiraIssueTypes();
    /// Si ese tipo del gestor es de los que piden un cambio («Improvement»; «Mejora» en lo guardado
    /// antes de que los tipos fueran los de la instancia). Lo demás, incluido lo vacío, es un error.
    static bool isImprovement(const QString& issueType);
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
