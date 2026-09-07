#pragma once

#include "core/models/RunHistory.h"

#include <optional>

namespace qaflow {

/// Persistencia del historial de ejecuciones. La implementación concreta vive en infrastructure/.
class IRunHistoryRepository {
public:
    virtual ~IRunHistoryRepository() = default;

    virtual std::optional<RunHistory> loadHistory() = 0;
    virtual bool saveHistory(const RunHistory& history) = 0;
};

} // namespace qaflow
