#include "TestCase.h"

#include <QCoreApplication>

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

QString label(Priority p) {
    switch (p) {
        case Priority::Alta: return QCoreApplication::translate("core", "Alta");
        case Priority::Media: return QCoreApplication::translate("core", "Media");
        case Priority::Baja: return QCoreApplication::translate("core", "Baja");
    }
    return {};
}

QString label(CaseStatus s) {
    switch (s) {
        case CaseStatus::Listo: return QCoreApplication::translate("core", "Listo");
        case CaseStatus::Borrador: return QCoreApplication::translate("core", "Borrador");
        case CaseStatus::Obsoleto: return QCoreApplication::translate("core", "Obsoleto");
    }
    return {};
}

QString label(RunOutcome o) {
    switch (o) {
        case RunOutcome::Passed: return QCoreApplication::translate("core", "Pasó");
        case RunOutcome::Failed: return QCoreApplication::translate("core", "Falló");
        case RunOutcome::Blocked: return QCoreApplication::translate("core", "Bloqueado");
        case RunOutcome::None: return QCoreApplication::translate("core", "Sin ejecutar");
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
    if (outcome == RunOutcome::None || !at.isValid()) return qaflow::label(RunOutcome::None);
    const QString verb = qaflow::label(outcome);

    const qint64 secs = at.secsTo(now);
    QString when;
    if (secs < 60) when = QCoreApplication::translate("core", "ahora");
    else if (secs < 3600) when = QCoreApplication::translate("core", "hace %1 min").arg(secs / 60);
    else if (at.date() == now.date()) when = QCoreApplication::translate("core", "hace %1 h").arg(secs / 3600);
    else if (at.date() == now.date().addDays(-1)) when = QCoreApplication::translate("core", "ayer");
    else when = QCoreApplication::translate("core", "hace %1 d").arg(at.date().daysTo(now.date()));
    return verb + QStringLiteral(" · ") + when;
}

QString Screenshot::extension() const {
    const QString name = fileName.isEmpty() ? path : fileName;
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot < 0 ? QString() : name.mid(dot + 1).toLower();
}

bool Screenshot::isImage() const {
    static const QStringList kImages{QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("webp"),
                                     QStringLiteral("gif"), QStringLiteral("bmp")};
    return kImages.contains(extension());
}

bool Screenshot::isAnimation() const { return extension() == QStringLiteral("gif"); }

QList<Screenshot> TestCase::shotsOfRun(const QString& runId) const {
    QList<Screenshot> out;
    for (const auto& s : shots) if (s.runId == runId) out << s;
    return out;
}

QString TestCase::latestEvidenceRunId() const {
    // Los ids del historial crecen (R-0007 > R-0006), así que el mayor es el más reciente; y una
    // ejecución en curso (sin id) manda sobre todas.
    QString latest;
    bool live = false;
    for (const auto& s : shots) {
        if (s.runId.isEmpty()) live = true;
        else if (s.runId > latest) latest = s.runId;
    }
    return live ? QString() : latest;
}

QList<Screenshot> TestCase::latestEvidence() const { return shotsOfRun(latestEvidenceRunId()); }

int TestCase::unassignedShots() const {
    int n = 0;
    for (const auto& s : shots) if (s.step == 0 && s.runId.isEmpty()) ++n;
    return n;
}

QString TestCase::searchText() const {
    return (QStringList{id, title, suite, component, jiraKey, testKey} + tags).join(QLatin1Char(' ')).toLower();
}

QStringList parseTags(const QString& text) {
    QStringList out;
    for (const auto& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QString t = part.trimmed();
        if (!t.isEmpty() && !out.contains(t, Qt::CaseInsensitive)) out << t;
    }
    return out;
}

bool TestCase::readyToBeMarkedListo() const {
    if (title.trimmed().isEmpty() || steps.isEmpty()) return false;
    for (const auto& s : steps) if (!s.isComplete()) return false;
    return true;
}

} // namespace qaflow
