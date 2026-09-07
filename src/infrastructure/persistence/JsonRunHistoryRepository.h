#pragma once

#include "core/services/IRunHistoryRepository.h"

#include <QString>

namespace qaflow {

/// Historial en un único fichero `history.json` dentro del directorio de datos.
class JsonRunHistoryRepository : public IRunHistoryRepository {
public:
    explicit JsonRunHistoryRepository(const QString& dataDir);

    std::optional<RunHistory> loadHistory() override;
    bool saveHistory(const RunHistory& history) override;

private:
    QString m_path;
};

} // namespace qaflow
