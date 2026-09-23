#include "AiService.h"

#include <QCoreApplication>
#include <QPointer>

namespace qaflow {

AiService::AiService(std::shared_ptr<IAiClient> client, SettingsStore& settings, QObject* parent)
    : QObject(parent), m_client(std::move(client)), m_settings(settings) {}

bool AiService::isConfigured() const { return m_settings.ai().isConfigured(); }

QString AiService::destination() const {
    const AiSettings& ai = m_settings.ai();
    return label(ai.provider) + QStringLiteral(" · ") + ai.model();
}

void AiService::generate(const QString& prompt, std::function<void(const AiCompletion&)> done) {
    m_client->complete(m_settings.ai(), prompt, std::move(done));
}

void AiService::fetchModels(std::function<void(const AiModelList&)> done) {
    m_client->listModels(m_settings.ai(), std::move(done));
}

void AiService::testConnection(std::function<void(const ConnectionResult&)> done) {
    const AiSettings tested = m_settings.ai();
    QPointer<AiService> self(this);
    m_client->listModels(tested, [self, tested, done](const AiModelList& r) {
        if (!self) return;
        // Sólo cuenta si los ajustes no cambiaron mientras se probaba (otra clave, otro proveedor).
        const AiSettings& now = self->m_settings.ai();
        if (now.provider == tested.provider && now.active().apiKey == tested.active().apiKey)
            self->m_settings.updateAi([&r](AiSettings& a) { a.of(a.provider).connected = r.ok; });
        if (!r.ok) { done(ConnectionResult{false, {}, r.error}); return; }
        QString summary = QCoreApplication::translate("application", "%1 · %2 modelos disponibles").arg(label(tested.provider)).arg(r.models.size());
        if (!r.models.isEmpty() && !r.models.contains(tested.model()))
            summary += QCoreApplication::translate("application", " · «%1» no está entre ellos: elige otro").arg(tested.model());
        done(ConnectionResult{true, summary, {}});
    });
}

} // namespace qaflow
