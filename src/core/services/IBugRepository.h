#pragma once

#include "core/models/IssueLink.h"

#include <optional>

namespace qaflow {

/// Persistencia del libro de bugs (issues creados y cola de pendientes).
class IBugRepository {
public:
    virtual ~IBugRepository() = default;

    virtual std::optional<BugLedger> loadLedger() = 0;
    virtual bool saveLedger(const BugLedger& ledger) = 0;
};

} // namespace qaflow
