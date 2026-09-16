#include "RequirementSourceService.h"

#include <algorithm>

namespace qaflow {

RequirementSourceService::RequirementSourceService(std::shared_ptr<IRequirementSource> source, SettingsStore& settings, QObject* parent)
    : QObject(parent), m_source(std::move(source)), m_settings(settings) {}

void RequirementSourceService::testConnection(std::function<void(const ConnectionResult&)> done) {
    const RequirementSourceSettings s = m_settings.requirementSource();
    m_source->testConnection(s, [this, s, done](const ConnectionResult& result) {
        m_settings.updateRequirementSource([&result](RequirementSourceSettings& r) { r.connected = result.ok; });
        if (!result.ok) { done(result); return; }
        // Con la sesión ya iniciada, la bandeja dice en qué sistemas tiene trabajo el usuario: son los que
        // tiene sentido vincular a un proyecto.
        m_source->fetchInbox(s, [this, result, done](const RequirementInboxResult& inbox) {
            if (inbox.ok) rememberSystems(inbox.requirements);
            done(result);
        });
    });
}

void RequirementSourceService::fetchInbox(std::function<void(const RequirementInboxResult&)> done) {
    m_source->fetchInbox(m_settings.requirementSource(), [this, done](const RequirementInboxResult& inbox) {
        if (inbox.ok) rememberSystems(inbox.requirements);
        done(inbox);
    });
}

void RequirementSourceService::fetchDetail(const QString& id, std::function<void(const RequirementDetailResult&)> done) {
    m_source->fetchDetail(m_settings.requirementSource(), id, std::move(done));
}

void RequirementSourceService::fetchSystems(std::function<void(const RequirementSystemsResult&)> done) {
    m_source->fetchSystems(m_settings.requirementSource(), std::move(done));
}

bool RequirementSourceService::canRegisterResult() const {
    const RequirementSourceSettings settings = m_settings.requirementSource();
    return m_source && m_source->canRegisterResult() && !settings.baseUrl().isEmpty() && !settings.user.trimmed().isEmpty();
}

QString RequirementSourceService::registrationProblem(const RequirementRegistration& registration) const {
    return m_source ? m_source->registrationProblem(registration) : QString();
}

void RequirementSourceService::registerResult(const RequirementRegistration& registration,
                                              std::function<void(const RequirementRegistrationResult&)> done) {
    if (!canRegisterResult()) {
        RequirementRegistrationResult result;
        result.failure = RequirementSourceFailure::Configuration;
        result.error = tr("Configura la conexión con GESREQ en Ajustes para registrar el resultado");
        done(result);
        return;
    }
    m_source->registerResult(m_settings.requirementSource(), registration, std::move(done));
}

void RequirementSourceService::rememberSystems(const QList<ExternalRequirement>& inbox) {
    QStringList systems;
    for (const auto& r : inbox)
        if (!r.systemCode.isEmpty() && !systems.contains(r.systemCode)) systems << r.systemCode;
    std::sort(systems.begin(), systems.end(), [](const QString& a, const QString& b) { return a.localeAwareCompare(b) < 0; });
    if (systems == m_systems) return;
    m_systems = systems;
    emit systemsChanged();
}

} // namespace qaflow
