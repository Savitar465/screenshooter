#pragma once

#include "core/models/Issue.h"

#include <optional>

namespace qaflow {

/// Persistencia de los issues de un proyecto.
class IIssueRepository {
public:
    virtual ~IIssueRepository() = default;

    /// Lista vacía si todavía no hay ninguno; nullopt si hay datos que no se pueden leer (y entonces no
    /// deben sobrescribirse, que se perderían los issues).
    virtual std::optional<QList<Issue>> loadIssues() = 0;
    virtual bool saveIssues(const QList<Issue>& issues) = 0;
};

} // namespace qaflow
