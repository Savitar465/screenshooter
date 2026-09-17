#include "Issue.h"

#include <QCoreApplication>

#include <algorithm>

namespace qaflow {

QString toString(IssueState s) {
    switch (s) {
        case IssueState::Pending: return QStringLiteral("Pendiente");
        case IssueState::Preparing: return QStringLiteral("En preparación");
        case IssueState::Testing: return QStringLiteral("En pruebas");
        case IssueState::Done: return QStringLiteral("Finalizado");
    }
    return QStringLiteral("Pendiente");
}

IssueState issueStateFromString(const QString& s) {
    for (const auto state : {IssueState::Pending, IssueState::Preparing, IssueState::Testing, IssueState::Done})
        if (s.trimmed().compare(toString(state), Qt::CaseInsensitive) == 0) return state;
    return IssueState::Pending;
}

QString label(IssueState s) {
    switch (s) {
        case IssueState::Pending: return QCoreApplication::translate("core", "Pendiente");
        case IssueState::Preparing: return QCoreApplication::translate("core", "En preparación");
        case IssueState::Testing: return QCoreApplication::translate("core", "En pruebas");
        case IssueState::Done: return QCoreApplication::translate("core", "Finalizado");
    }
    return {};
}

QString toString(QaOutcome o) {
    switch (o) {
        case QaOutcome::Pendiente: return QStringLiteral("Pendiente");
        case QaOutcome::Conforme: return QStringLiteral("Conforme");
        case QaOutcome::Observado: return QStringLiteral("Observado");
    }
    return QStringLiteral("Pendiente");
}

QaOutcome qaOutcomeFromString(const QString& s) {
    for (const auto outcome : {QaOutcome::Pendiente, QaOutcome::Conforme, QaOutcome::Observado})
        if (s.trimmed().compare(toString(outcome), Qt::CaseInsensitive) == 0) return outcome;
    return QaOutcome::Pendiente;
}

QString label(QaOutcome o) {
    switch (o) {
        case QaOutcome::Pendiente: return QCoreApplication::translate("core", "Pendiente");
        case QaOutcome::Conforme: return QCoreApplication::translate("core", "Conforme");
        case QaOutcome::Observado: return QCoreApplication::translate("core", "Observado");
    }
    return {};
}

const IssueRevision* Issue::currentRevision() const {
    if (revisions.isEmpty()) return nullptr;
    const IssueRevision& last = revisions.last();
    return last.isOpen() ? &last : nullptr;
}

const IssueRevision* Issue::revision(int number) const {
    if (revisions.isEmpty()) return nullptr;
    if (number <= 0) return &revisions.last();
    for (const auto& round : revisions)
        if (round.number == number) return &round;
    return nullptr;
}

int Issue::currentRevisionNumber() const { return revisions.isEmpty() ? 0 : revisions.last().number; }

const IssueRevision* Issue::lastClosedRevision() const {
    for (auto it = revisions.crbegin(); it != revisions.crend(); ++it)
        if (!it->isOpen()) return &*it;
    return nullptr;
}

QaOutcome Issue::lastOutcome() const {
    const IssueRevision* closed = lastClosedRevision();
    return closed ? closed->outcome : QaOutcome::Pendiente;
}

QString Issue::searchText() const {
    const ExternalRequirement& r = requirement.data;
    QStringList parts{id, title, notes, publication.key, r.id, r.system, r.summary, r.requester, r.requestingUnit, r.user};
    parts << r.states << requirement.detail.reference << requirement.detail.description;
    return parts.join(QLatin1Char(' '));
}

bool IssueFilter::matches(const Issue& issue) const {
    if (state && issue.state != *state) return false;
    if (priority && issue.priority != *priority) return false;
    if (published && issue.isPublished() != *published) return false;
    const QString haystack = issue.searchText();
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    return std::all_of(words.cbegin(), words.cend(), [&haystack](const QString& word) { return haystack.contains(word, Qt::CaseInsensitive); });
}

Priority priorityFromRequirement(const QString& text) {
    const QString t = text.trimmed().toLower();
    if (t.startsWith(QStringLiteral("alt")) || t.startsWith(QStringLiteral("high")) || t.startsWith(QStringLiteral("urgent"))) return Priority::Alta;
    if (t.startsWith(QStringLiteral("baj")) || t.startsWith(QStringLiteral("low"))) return Priority::Baja;
    return Priority::Media;
}

QList<RequirementChange> diffRequirement(const ExternalRequirement& before, const ExternalRequirement& after) {
    QList<RequirementChange> out;
    auto compare = [&out](const char* field, const QString& a, const QString& b) {
        if (a.simplified() != b.simplified()) out << RequirementChange{QString::fromLatin1(field), a.simplified(), b.simplified()};
    };
    auto date = [](const QDate& d) { return d.isValid() ? d.toString(QStringLiteral("dd/MM/yyyy")) : QString(); };
    const QString separator = QStringLiteral(" + ");
    // Primero lo que más importa a QA: en qué estado está y qué se pide.
    compare("states", before.states.join(separator), after.states.join(separator));
    compare("summary", before.summary, after.summary);
    compare("priority", before.priority, after.priority);
    compare("system", before.system, after.system);
    compare("assignedFrom", date(before.assignedFrom), date(after.assignedFrom));
    compare("assignedUntil", date(before.assignedUntil), date(after.assignedUntil));
    compare("requester", before.requester, after.requester);
    compare("requestingUnit", before.requestingUnit, after.requestingUnit);
    compare("user", before.user, after.user);
    return out;
}

QList<RequirementChange> mergeChanges(const QList<RequirementChange>& pending, const QList<RequirementChange>& incoming) {
    QList<RequirementChange> out = pending;
    for (const auto& change : incoming) {
        const auto it = std::find_if(out.begin(), out.end(), [&change](const RequirementChange& c) { return c.field == change.field; });
        if (it == out.end()) {
            out << change;
            continue;
        }
        it->after = change.after;
        if (it->before.simplified() == it->after.simplified()) out.erase(it);
    }
    return out;
}

QString requirementFieldLabel(const QString& field) {
    if (field == QLatin1String("states")) return QCoreApplication::translate("core", "Estado");
    if (field == QLatin1String("summary")) return QCoreApplication::translate("core", "Descripción corta");
    if (field == QLatin1String("priority")) return QCoreApplication::translate("core", "Prioridad");
    if (field == QLatin1String("system")) return QCoreApplication::translate("core", "Sistema");
    if (field == QLatin1String("assignedFrom")) return QCoreApplication::translate("core", "Asignado desde");
    if (field == QLatin1String("assignedUntil")) return QCoreApplication::translate("core", "Asignado hasta");
    if (field == QLatin1String("requester")) return QCoreApplication::translate("core", "Funcionario solicitante");
    if (field == QLatin1String("requestingUnit")) return QCoreApplication::translate("core", "Unidad solicitante");
    if (field == QLatin1String("user")) return QCoreApplication::translate("core", "Usuario");
    return field;
}

} // namespace qaflow
