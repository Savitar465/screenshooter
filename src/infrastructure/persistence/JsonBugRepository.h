#pragma once

#include "core/services/IBugRepository.h"

#include <QString>

namespace qaflow {

/// Libro de bugs en `bugs.json` dentro del directorio de datos.
class JsonBugRepository : public IBugRepository {
public:
    explicit JsonBugRepository(const QString& dataDir);

    std::optional<BugLedger> loadLedger() override;
    bool saveLedger(const BugLedger& ledger) override;

private:
    QString m_path;
};

} // namespace qaflow
