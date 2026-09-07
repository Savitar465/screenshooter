#include "RunHistory.h"

namespace qaflow {

QString toString(StepResult r) {
    switch (r) {
        case StepResult::Pass: return QStringLiteral("Pasa");
        case StepResult::Fail: return QStringLiteral("Falla");
        case StepResult::Block: return QStringLiteral("Bloqueado");
    }
    return {};
}

QString toString(Verdict v) {
    switch (v) {
        case Verdict::Superado: return QStringLiteral("Superado");
        case Verdict::Fallido: return QStringLiteral("Fallido");
        case Verdict::Bloqueado: return QStringLiteral("Bloqueado");
    }
    return {};
}

StepResult stepResultFromString(const QString& s) {
    if (s.compare(QStringLiteral("Falla"), Qt::CaseInsensitive) == 0) return StepResult::Fail;
    if (s.compare(QStringLiteral("Bloqueado"), Qt::CaseInsensitive) == 0) return StepResult::Block;
    return StepResult::Pass;
}

Verdict verdictFromString(const QString& s) {
    if (s.compare(QStringLiteral("Fallido"), Qt::CaseInsensitive) == 0) return Verdict::Fallido;
    if (s.compare(QStringLiteral("Bloqueado"), Qt::CaseInsensitive) == 0) return Verdict::Bloqueado;
    return Verdict::Superado;
}

QString formatDuration(qint64 secs) {
    if (secs < 0) secs = 0;
    if (secs < 60) return QStringLiteral("%1 s").arg(secs);
    if (secs < 3600) return QStringLiteral("%1 min %2 s").arg(secs / 60).arg(secs % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1 h %2 min").arg(secs / 3600).arg((secs % 3600) / 60, 2, 10, QLatin1Char('0'));
}

int RunRecord::count(StepResult r) const {
    int n = 0;
    for (const auto& s : steps) if (s.result == r) ++n;
    return n;
}

bool RunRecord::hasNotes() const {
    for (const auto& s : steps) if (!s.note.trimmed().isEmpty()) return true;
    return false;
}

} // namespace qaflow
