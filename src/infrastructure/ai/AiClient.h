#pragma once

#include "core/services/IAiClient.h"
#include "infrastructure/http/HttpClient.h"

namespace qaflow {

/// Las API de mensajes de los tres proveedores, cada una con su forma:
///
/// - Anthropic: `POST /v1/messages` con `x-api-key` y `anthropic-version`; el texto está en `content[]`.
/// - OpenAI: `POST /chat/completions` con `Authorization: Bearer`, pidiendo un objeto JSON
///   (`response_format`); el texto está en `choices[0].message.content`. Con otra dirección vale para
///   servidores compatibles (Azure con proxy, Ollama, LM Studio…).
/// - Gemini: `POST /models/{modelo}:generateContent` con `x-goog-api-key`, pidiendo
///   `application/json`; el texto está en `candidates[0].content.parts[]`.
///
/// Los errores se traducen a algo accionable (clave inválida, modelo inexistente, límite de uso) con el
/// mensaje del proveedor detrás. La clave nunca aparece en los mensajes.
class AiClient : public HttpClient, public IAiClient {
    Q_OBJECT
public:
    explicit AiClient(QObject* parent = nullptr) : HttpClient(parent) {}

    void complete(const AiSettings& s, const QString& prompt, std::function<void(const AiCompletion&)> done) override;
    void listModels(const AiSettings& s, std::function<void(const AiModelList&)> done) override;

    /// Una generación larga (muchos casos) puede tardar minutos: se espera hasta esto.
    int completionTimeoutMs = 240000;

private:
    QNetworkRequest request(const AiSettings& s, const QString& path, int timeoutMs) const;
    static QString failureOf(const AiSettings& s, const Response& r, bool completing);
};

} // namespace qaflow
