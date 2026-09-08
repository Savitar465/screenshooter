#include "BugReport.h"

#include <QCoreApplication>

namespace qaflow {

QStringList BugReport::severities() {
    return {QStringLiteral("Bloqueante"), QStringLiteral("Crítica"), QStringLiteral("Mayor"), QStringLiteral("Menor"), QStringLiteral("Trivial")};
}

QStringList BugReport::environments() {
    return {QStringLiteral("Staging"), QStringLiteral("QA"), QStringLiteral("Producción")};
}

QString BugReport::severityLabel(const QString& severity) {
    if (severity == QStringLiteral("Bloqueante")) return QCoreApplication::translate("core", "Bloqueante");
    if (severity == QStringLiteral("Crítica")) return QCoreApplication::translate("core", "Crítica");
    if (severity == QStringLiteral("Mayor")) return QCoreApplication::translate("core", "Mayor");
    if (severity == QStringLiteral("Menor")) return QCoreApplication::translate("core", "Menor");
    if (severity == QStringLiteral("Trivial")) return QCoreApplication::translate("core", "Trivial");
    return severity;
}

QString BugReport::environmentLabel(const QString& environment) {
    if (environment == QStringLiteral("Producción")) return QCoreApplication::translate("core", "Producción");
    return environment;
}

QString BugReport::jiraPriorityFor(const QString& severity) {
    if (severity == QStringLiteral("Bloqueante")) return QStringLiteral("Highest");
    if (severity == QStringLiteral("Crítica")) return QStringLiteral("High");
    if (severity == QStringLiteral("Mayor")) return QStringLiteral("Medium");
    if (severity == QStringLiteral("Menor")) return QStringLiteral("Low");
    if (severity == QStringLiteral("Trivial")) return QStringLiteral("Lowest");
    return {};
}

QString BugReport::jiraDescription() const {
    QString d;
    d += QCoreApplication::translate("core", "h3. Entorno\n%1\n\n").arg(environmentLabel(environment));
    d += QCoreApplication::translate("core", "h3. Severidad\n%1\n\n").arg(severityLabel(severity));
    d += QCoreApplication::translate("core", "h3. Caso vinculado\n%1\n\n").arg(linkedCaseId.isEmpty() ? QStringLiteral("—") : linkedCaseId);
    if (!linkedStoryKey.isEmpty()) d += QCoreApplication::translate("core", "h3. Historia relacionada\n%1\n\n").arg(linkedStoryKey);
    d += QCoreApplication::translate("core", "h3. Pasos para reproducir\n%1\n\n").arg(stepsToReproduce);
    d += QCoreApplication::translate("core", "h3. Resultado esperado\n%1\n\n").arg(expected);
    d += QCoreApplication::translate("core", "h3. Resultado actual\n%1\n").arg(actual);
    return d;
}

QString BugReport::markdownDescription(const QStringList& attachmentLinks) const {
    QStringList out;
    out << QCoreApplication::translate("core", "**Entorno:** %1 · **Severidad:** %2").arg(environmentLabel(environment), severityLabel(severity));
    out << QCoreApplication::translate("core", "**Caso vinculado:** %1").arg(linkedCaseId.isEmpty() ? QStringLiteral("—") : linkedCaseId);
    if (!linkedStoryKey.isEmpty()) out << QCoreApplication::translate("core", "**Historia relacionada:** %1").arg(linkedStoryKey);
    out << QString() << QCoreApplication::translate("core", "### Pasos para reproducir") << stepsToReproduce;
    out << QString() << QCoreApplication::translate("core", "### Resultado esperado") << expected;
    out << QString() << QCoreApplication::translate("core", "### Resultado actual") << actual;
    if (!attachmentLinks.isEmpty()) {
        out << QString() << QCoreApplication::translate("core", "### Capturas");
        for (const auto& l : attachmentLinks) out << l;
    }
    return out.join(QLatin1Char('\n')) + QLatin1Char('\n');
}

QString BugReport::htmlDescription() const {
    auto esc = [](const QString& s) { return s.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")); };
    QString d;
    d += QCoreApplication::translate("core", "<p><b>Entorno:</b> %1 · <b>Severidad:</b> %2</p>").arg(esc(environmentLabel(environment)), esc(severityLabel(severity)));
    d += QCoreApplication::translate("core", "<p><b>Caso vinculado:</b> %1</p>").arg(linkedCaseId.isEmpty() ? QStringLiteral("—") : esc(linkedCaseId));
    if (!linkedStoryKey.isEmpty()) d += QCoreApplication::translate("core", "<p><b>Historia relacionada:</b> %1</p>").arg(esc(linkedStoryKey));
    d += QCoreApplication::translate("core", "<h3>Pasos para reproducir</h3><p>%1</p>").arg(esc(stepsToReproduce));
    d += QCoreApplication::translate("core", "<h3>Resultado esperado</h3><p>%1</p>").arg(esc(expected));
    d += QCoreApplication::translate("core", "<h3>Resultado actual</h3><p>%1</p>").arg(esc(actual));
    return d;
}

} // namespace qaflow
