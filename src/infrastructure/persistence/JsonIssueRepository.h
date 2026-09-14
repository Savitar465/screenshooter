#pragma once

#include "core/services/IIssueRepository.h"

#include <QString>

namespace qaflow {

/// Issues del proyecto en `issues.json` dentro de su directorio de datos, con lo importado de GESREQ
/// (fila de la bandeja, ficha y cambios pendientes) aparte de lo escrito en QAflow.
class JsonIssueRepository : public IIssueRepository {
public:
    explicit JsonIssueRepository(const QString& dataDir);

    std::optional<QList<Issue>> loadIssues() override;
    bool saveIssues(const QList<Issue>& issues) override;

private:
    QString m_path;
};

} // namespace qaflow
