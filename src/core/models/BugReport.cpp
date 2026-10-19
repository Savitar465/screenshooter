#include "BugReport.h"

#include <QCoreApplication>
#include <QRegularExpression>

namespace qaflow {

namespace {
/// "qa.operador (Operador)", o sólo el usuario si no se dijo el rol.
QString userLine(const TestUser& u) {
    return u.role.trimmed().isEmpty() ? u.user.trimmed() : QStringLiteral("%1 (%2)").arg(u.user.trimmed(), u.role.trimmed());
}
QStringList filled(const QStringList& lines) {
    QStringList out;
    for (const auto& l : lines) if (!l.trimmed().isEmpty()) out << l.trimmed();
    return out;
}
QList<TestUser> filled(const QList<TestUser>& users) {
    QList<TestUser> out;
    for (const auto& u : users) if (!u.isEmpty()) out << u;
    return out;
}
} // namespace

QStringList BugReport::severities() {
    return {QStringLiteral("Bloqueante"), QStringLiteral("Crítica"), QStringLiteral("Mayor"), QStringLiteral("Menor"), QStringLiteral("Trivial")};
}

QStringList BugReport::environments() {
    return {QStringLiteral("Staging"), QStringLiteral("QA"), QStringLiteral("Producción")};
}

QStringList BugReport::jiraIssueTypes() {
    return {QStringLiteral("Bug"), QStringLiteral("Improvement")};
}

bool BugReport::isImprovement(const QString& issueType) {
    const QString t = issueType.trimmed();
    return t.compare(jiraIssueTypes().value(1), Qt::CaseInsensitive) == 0
           || t.compare(QStringLiteral("Mejora"), Qt::CaseInsensitive) == 0;
}

QStringList BugReport::classifications() {
    return {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), QStringLiteral("D"), QStringLiteral("E")};
}

QString BugReport::classificationName(const QString& classification) {
    if (classification == QStringLiteral("A")) return QStringLiteral("Funcionamiento/Lógica");
    if (classification == QStringLiteral("B")) return QStringLiteral("Datos");
    if (classification == QStringLiteral("C")) return QStringLiteral("Estético/Forma");
    if (classification == QStringLiteral("D")) return QStringLiteral("Recomendaciones");
    if (classification == QStringLiteral("E")) return QStringLiteral("Vulnerabilidades");
    return {};
}

QString BugReport::classificationLabel(const QString& classification) {
    const QString name = classificationName(classification);
    if (name.isEmpty()) return classification;
    QString translated = name;
    if (classification == QStringLiteral("A")) translated = QCoreApplication::translate("core", "Funcionamiento/Lógica");
    else if (classification == QStringLiteral("B")) translated = QCoreApplication::translate("core", "Datos");
    else if (classification == QStringLiteral("C")) translated = QCoreApplication::translate("core", "Estético/Forma");
    else if (classification == QStringLiteral("D")) translated = QCoreApplication::translate("core", "Recomendaciones");
    else if (classification == QStringLiteral("E")) translated = QCoreApplication::translate("core", "Vulnerabilidades");
    return QStringLiteral("%1 · %2").arg(classification, translated);
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

namespace {
/// Nombres con los que las instancias llaman a un bug, del preferido al último.
const QStringList& bugTypeNames() {
    static const QStringList names{QStringLiteral("Bug"), QStringLiteral("Error"), QStringLiteral("Defecto"), QStringLiteral("Defect")};
    return names;
}
} // namespace

QString BugReport::defaultIssueType(const QStringList& available) {
    if (available.isEmpty()) return QStringLiteral("Bug");
    // Cada instancia lo llama a su manera: se toma el nombre tal y como lo tiene el proyecto.
    for (const QString& wanted : bugTypeNames())
        for (const auto& t : available)
            if (t.trimmed().compare(wanted, Qt::CaseInsensitive) == 0) return t.trimmed();
    for (const auto& t : available)
        if (!isImprovement(t)) return t.trimmed();
    return available.first().trimmed();
}

QStringList BugReport::trackedIssueTypes(const QStringList& available) {
    if (available.isEmpty()) return jiraIssueTypes();
    // Sólo nombres que el proyecto tiene: Jira rechaza la búsqueda entera si uno no existe.
    QStringList out;
    const auto add = [&out](const QString& t) { if (!t.trimmed().isEmpty() && !out.contains(t.trimmed(), Qt::CaseInsensitive)) out << t.trimmed(); };
    add(defaultIssueType(available));
    for (const auto& t : available)
        if (bugTypeNames().contains(t.trimmed(), Qt::CaseInsensitive) || isImprovement(t)) add(t);
    return out;
}

QString BugReport::titleFromActual(const QString& suite, const QString& actual) {
    // La primera frase (o línea) de lo que pasó, sin el punto final: es lo que se lee en un listado.
    static const QRegularExpression sentenceEnd(QStringLiteral("[.!?;](\\s|$)"));
    const QString firstLine = actual.trimmed().section(QLatin1Char('\n'), 0, 0).simplified();
    const QRegularExpressionMatch m = sentenceEnd.match(firstLine);
    QString text = (m.hasMatch() ? firstLine.left(m.capturedStart()) : firstLine).trimmed();
    if (text.isEmpty()) return {};
    constexpr int kMax = 90;
    if (text.size() > kMax) text = text.left(kMax).section(QLatin1Char(' '), 0, -2).trimmed() + QStringLiteral("…");
    text[0] = text[0].toUpper();
    return suite.trimmed().isEmpty() ? text : QStringLiteral("[%1] %2").arg(suite.trimmed(), text);
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
    d += QCoreApplication::translate("core", "h3. Clasificación\n%1\n\n").arg(classificationLabel(classification));
    d += QCoreApplication::translate("core", "h3. Caso vinculado\n%1\n\n").arg(linkedCaseId.isEmpty() ? QStringLiteral("—") : linkedCaseId);
    if (!linkedStoryKey.isEmpty()) d += QCoreApplication::translate("core", "h3. Historia relacionada\n%1\n\n").arg(linkedStoryKey);
    if (const QList<TestUser> users = filled(testUsers); !users.isEmpty()) {
        QStringList lines;
        for (const auto& u : users) lines << QStringLiteral("* ") + userLine(u);
        d += QCoreApplication::translate("core", "h3. Usuarios utilizados\n%1\n\n").arg(lines.join(QLatin1Char('\n')));
    }
    QStringList steps;
    for (const auto& st : filled(stepsToReproduce)) steps << QStringLiteral("# ") + st;
    d += QCoreApplication::translate("core", "h3. Pasos para reproducir\n%1\n\n").arg(steps.isEmpty() ? QStringLiteral("—") : steps.join(QLatin1Char('\n')));
    d += QCoreApplication::translate("core", "h3. Resultado esperado\n%1\n\n").arg(expected);
    d += QCoreApplication::translate("core", "h3. Resultado actual\n%1\n").arg(actual);
    return d;
}

QString BugReport::markdownDescription(const QStringList& attachmentLinks) const {
    QStringList out;
    out << QCoreApplication::translate("core", "**Entorno:** %1 · **Severidad:** %2 · **Clasificación:** %3")
                .arg(environmentLabel(environment), severityLabel(severity), classificationLabel(classification));
    out << QCoreApplication::translate("core", "**Caso vinculado:** %1").arg(linkedCaseId.isEmpty() ? QStringLiteral("—") : linkedCaseId);
    if (!linkedStoryKey.isEmpty()) out << QCoreApplication::translate("core", "**Historia relacionada:** %1").arg(linkedStoryKey);
    if (const QList<TestUser> users = filled(testUsers); !users.isEmpty()) {
        out << QString() << QCoreApplication::translate("core", "### Usuarios utilizados");
        for (const auto& u : users) out << QStringLiteral("- ") + userLine(u);
    }
    out << QString() << QCoreApplication::translate("core", "### Pasos para reproducir");
    const QStringList steps = filled(stepsToReproduce);
    for (int i = 0; i < steps.size(); ++i) out << QStringLiteral("%1. %2").arg(i + 1).arg(steps[i]);
    if (steps.isEmpty()) out << QStringLiteral("—");
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
    d += QCoreApplication::translate("core", "<p><b>Entorno:</b> %1 · <b>Severidad:</b> %2 · <b>Clasificación:</b> %3</p>")
                 .arg(esc(environmentLabel(environment)), esc(severityLabel(severity)), esc(classificationLabel(classification)));
    d += QCoreApplication::translate("core", "<p><b>Caso vinculado:</b> %1</p>").arg(linkedCaseId.isEmpty() ? QStringLiteral("—") : esc(linkedCaseId));
    if (!linkedStoryKey.isEmpty()) d += QCoreApplication::translate("core", "<p><b>Historia relacionada:</b> %1</p>").arg(esc(linkedStoryKey));
    if (const QList<TestUser> users = filled(testUsers); !users.isEmpty()) {
        QString items;
        for (const auto& u : users) items += QStringLiteral("<li>%1</li>").arg(esc(userLine(u)));
        d += QCoreApplication::translate("core", "<h3>Usuarios utilizados</h3><ul>%1</ul>").arg(items);
    }
    QString steps;
    for (const auto& st : filled(stepsToReproduce)) steps += QStringLiteral("<li>%1</li>").arg(esc(st));
    d += QCoreApplication::translate("core", "<h3>Pasos para reproducir</h3>%1").arg(steps.isEmpty() ? QStringLiteral("<p>—</p>") : QStringLiteral("<ol>%1</ol>").arg(steps));
    d += QCoreApplication::translate("core", "<h3>Resultado esperado</h3><p>%1</p>").arg(esc(expected));
    d += QCoreApplication::translate("core", "<h3>Resultado actual</h3><p>%1</p>").arg(esc(actual));
    return d;
}

} // namespace qaflow
