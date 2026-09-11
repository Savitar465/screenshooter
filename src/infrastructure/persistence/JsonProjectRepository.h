#pragma once
#include "core/services/IProjectRepository.h"

namespace qaflow {
class JsonProjectRepository : public IProjectRepository {
public:
    explicit JsonProjectRepository(QString root);
    std::optional<ProjectCollection> load() override;
    bool save(const ProjectCollection& collection) override;
    bool initialize(const QString& id) override;
    QString dataDir(const QString& id) const override;
private:
    QString m_root;
};
} // namespace qaflow
