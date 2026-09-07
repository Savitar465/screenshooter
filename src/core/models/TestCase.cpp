#include "TestCase.h"

namespace qaflow {

QString toString(Priority p) {
    switch (p) {
        case Priority::Alta: return QStringLiteral("Alta");
        case Priority::Media: return QStringLiteral("Media");
        case Priority::Baja: return QStringLiteral("Baja");
    }
    return {};
}

QString toString(CaseStatus s) {
    switch (s) {
        case CaseStatus::Listo: return QStringLiteral("Listo");
        case CaseStatus::Borrador: return QStringLiteral("Borrador");
        case CaseStatus::Obsoleto: return QStringLiteral("Obsoleto");
    }
    return {};
}

Priority priorityFromString(const QString& s) {
    if (s.compare(QStringLiteral("Alta"), Qt::CaseInsensitive) == 0) return Priority::Alta;
    if (s.compare(QStringLiteral("Baja"), Qt::CaseInsensitive) == 0) return Priority::Baja;
    return Priority::Media;
}

CaseStatus statusFromString(const QString& s) {
    if (s.compare(QStringLiteral("Listo"), Qt::CaseInsensitive) == 0) return CaseStatus::Listo;
    if (s.compare(QStringLiteral("Obsoleto"), Qt::CaseInsensitive) == 0) return CaseStatus::Obsoleto;
    return CaseStatus::Borrador;
}

QString LastRun::label(const QDateTime& now) const {
    if (outcome == RunOutcome::None || !at.isValid()) return QStringLiteral("Sin ejecutar");
    const QString verb = outcome == RunOutcome::Passed ? QStringLiteral("Pasó")
                       : outcome == RunOutcome::Failed ? QStringLiteral("Falló")
                                                       : QStringLiteral("Bloqueado");

    const qint64 secs = at.secsTo(now);
    QString when;
    if (secs < 60) when = QStringLiteral("ahora");
    else if (secs < 3600) when = QStringLiteral("hace %1 min").arg(secs / 60);
    else if (at.date() == now.date()) when = QStringLiteral("hace %1 h").arg(secs / 3600);
    else if (at.date() == now.date().addDays(-1)) when = QStringLiteral("ayer");
    else when = QStringLiteral("hace %1 d").arg(at.date().daysTo(now.date()));
    return verb + QStringLiteral(" · ") + when;
}

int TestCase::unassignedShots() const {
    int n = 0;
    for (const auto& s : shots) if (s.step == 0) ++n;
    return n;
}

bool TestCase::readyToBeMarkedListo() const {
    if (title.trimmed().isEmpty() || steps.isEmpty()) return false;
    for (const auto& s : steps) if (!s.isComplete()) return false;
    return true;
}

} // namespace qaflow
