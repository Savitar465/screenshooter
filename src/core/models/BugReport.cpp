#include "BugReport.h"

namespace qaflow {

QString BugReport::jiraDescription() const {
    QString d;
    d += QStringLiteral("h3. Entorno\n%1\n\n").arg(environment);
    d += QStringLiteral("h3. Caso vinculado\n%1\n\n").arg(linkedCaseId.isEmpty() ? QStringLiteral("—") : linkedCaseId);
    if (!linkedStoryKey.isEmpty()) d += QStringLiteral("h3. Historia relacionada\n%1\n\n").arg(linkedStoryKey);
    d += QStringLiteral("h3. Pasos para reproducir\n%1\n\n").arg(stepsToReproduce);
    d += QStringLiteral("h3. Resultado esperado\n%1\n\n").arg(expected);
    d += QStringLiteral("h3. Resultado actual\n%1\n").arg(actual);
    return d;
}

} // namespace qaflow
