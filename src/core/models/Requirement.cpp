#include "Requirement.h"

#include <QUrl>

namespace qaflow {

QString RequirementSourceSettings::baseUrl() const {
    QString base = url.trimmed();
    while (base.endsWith(QLatin1Char('/'))) base.chop(1);
    return base;
}

QString RequirementSourceSettings::resolve(const QString& path) const {
    if (baseUrl().isEmpty()) return {};
    return QUrl(baseUrl() + QLatin1Char('/')).resolved(QUrl(path.trimmed())).toString();
}

QString RequirementDetail::field(const QString& label) const {
    auto same = [&label](const RequirementField& f) { return f.label.compare(label, Qt::CaseInsensitive) == 0; };
    for (const auto& f : fields)
        if (same(f)) return f.value;
    for (const auto& s : sections)
        for (const auto& f : s.fields)
            if (same(f)) return f.value;
    return {};
}

QPair<QString, QString> splitSystem(const QString& system) {
    const QString text = system.simplified();
    // El código puede llevar espacios ("SUMA TRANSITO") y el nombre, guiones ("SISTEMA SUMA - MODULO
    // DE INGRESO"): lo que separa uno de otro es el primero.
    const qsizetype dash = text.indexOf(QLatin1Char('-'));
    if (dash < 0) return {text, QString()};
    return {text.left(dash).trimmed(), text.mid(dash + 1).trimmed()};
}

} // namespace qaflow
