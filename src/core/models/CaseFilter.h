#pragma once

#include "core/models/TestCase.h"

#include <QString>
#include <optional>

namespace qaflow {

/// Criterios de la lista de casos. Un campo vacío/nulo no filtra.
struct CaseFilter {
    QString text;                          // búsqueda libre (ver TestCase::searchText)
    QString suite;                         // vacío = todas
    std::optional<CaseStatus> status;
    std::optional<Priority> priority;
    std::optional<RunOutcome> outcome;     // RunOutcome::None = "sin ejecutar"

    bool isEmpty() const { return text.trimmed().isEmpty() && suite.isEmpty() && !status && !priority && !outcome; }

    bool matches(const TestCase& c) const {
        if (!suite.isEmpty() && c.suite != suite) return false;
        if (status && c.status != *status) return false;
        if (priority && c.priority != *priority) return false;
        if (outcome && c.lastRun.outcome != *outcome) return false;
        const QString q = text.trimmed().toLower();
        return q.isEmpty() || c.searchText().contains(q);
    }
};

} // namespace qaflow
