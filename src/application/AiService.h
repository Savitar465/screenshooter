#pragma once

#include "application/SettingsStore.h"
#include "core/services/IAiClient.h"
#include "core/services/IIssueTracker.h"   // ConnectionResult

#include <QObject>
#include <functional>
#include <memory>

namespace qaflow {

/// Generación con la IA configurada en Ajustes (proveedor, modelo y clave del usuario). Nunca envía nada
/// por su cuenta: quien lo llama es una acción explícita sobre un prompt que el usuario ya revisó.
class AiService : public QObject {
    Q_OBJECT
public:
    AiService(std::shared_ptr<IAiClient> client, SettingsStore& settings, QObject* parent = nullptr);

    /// Hay clave para el proveedor elegido.
    bool isConfigured() const;
    /// "Anthropic (Claude) · claude-sonnet-5": a quién y con qué modelo se enviaría.
    QString destination() const;

    void generate(const QString& prompt, std::function<void(const AiCompletion&)> done);
    /// Lista los modelos con la clave y deja en los ajustes si entró. Avisa si el modelo elegido no está.
    void testConnection(std::function<void(const ConnectionResult&)> done);
    void fetchModels(std::function<void(const AiModelList&)> done);

private:
    std::shared_ptr<IAiClient> m_client;
    SettingsStore& m_settings;
};

} // namespace qaflow
