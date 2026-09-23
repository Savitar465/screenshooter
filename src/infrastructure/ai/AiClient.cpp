#include "AiClient.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

#include <algorithm>

namespace qaflow {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("infrastructure", text); }

constexpr int kListTimeoutMs = 20000;

/// El mensaje que el proveedor pone en el error: {"error": {"message": …}} en los tres.
QString providerMessage(const QJsonDocument& json) {
    const QJsonValue error = json.object().value(QStringLiteral("error"));
    if (error.isObject()) return error.toObject().value(QStringLiteral("message")).toString().trimmed();
    if (error.isString()) return error.toString().trimmed();
    return {};
}

/// Modelos de OpenAI que no escriben texto (embeddings, audio, imagen…): no se ofrecen para generar casos.
bool isChatModel(const QString& id) {
    static const QStringList notChat{QStringLiteral("embedding"), QStringLiteral("tts"), QStringLiteral("whisper"),
                                     QStringLiteral("dall-e"), QStringLiteral("moderation"), QStringLiteral("image"),
                                     QStringLiteral("audio"), QStringLiteral("realtime"), QStringLiteral("transcribe"),
                                     QStringLiteral("search"), QStringLiteral("babbage"), QStringLiteral("davinci")};
    return std::none_of(notChat.cbegin(), notChat.cend(), [&id](const QString& word) { return id.contains(word); });
}

QString geminiModel(const QString& model) {
    return model.startsWith(QLatin1String("models/")) ? model.mid(7) : model;
}

} // namespace

QNetworkRequest AiClient::request(const AiSettings& s, const QString& path, int timeoutMs) const {
    QNetworkRequest req = jsonRequest(s.baseUrl() + path);
    req.setTransferTimeout(timeoutMs);
    const QByteArray key = s.active().apiKey.trimmed().toUtf8();
    switch (s.provider) {
        case AiProvider::Anthropic:
            req.setRawHeader("x-api-key", key);
            req.setRawHeader("anthropic-version", "2023-06-01");
            break;
        case AiProvider::OpenAI:
            req.setRawHeader("Authorization", "Bearer " + key);
            break;
        case AiProvider::Gemini:
            req.setRawHeader("x-goog-api-key", key);
            break;
    }
    return req;
}

QString AiClient::failureOf(const AiSettings& s, const Response& r, bool completing) {
    const QString who = label(s.provider);
    const QString detail = providerMessage(r.json);
    QString what;
    if (r.status == 0) what = tr("No se pudo conectar con %1").arg(who);
    else if (r.status == 401 || r.status == 403) what = tr("%1 rechazó la clave de API (no es válida o no tiene permiso)").arg(who);
    else if (r.status == 404 && completing) what = tr("%1 no tiene el modelo «%2» para esta clave").arg(who, s.model());
    else if (r.status == 429) what = tr("%1 alcanzó el límite de uso de la clave: espera un momento o revisa el plan").arg(who);
    else if (r.status == 529 || r.status == 503) what = tr("%1 está saturado: vuelve a intentarlo en un momento").arg(who);
    else if (r.status == 400 && completing) what = tr("%1 no aceptó la petición").arg(who);
    else what = tr("%1 respondió con un error (HTTP %2)").arg(who).arg(r.status);
    if (!detail.isEmpty()) return what + QStringLiteral(" · ") + detail;
    if (r.status == 0 && !r.error.isEmpty()) return what + QStringLiteral(" · ") + r.error;
    return what;
}

void AiClient::complete(const AiSettings& s, const QString& prompt, std::function<void(const AiCompletion&)> done) {
    if (!s.isConfigured()) {
        done(AiCompletion{false, {}, false, {}, tr("Falta la clave de API de %1 en Ajustes").arg(label(s.provider)), false});
        return;
    }
    const QString model = s.model();
    QString path;
    QJsonObject body;
    switch (s.provider) {
        case AiProvider::Anthropic:
            path = QStringLiteral("/v1/messages");
            body = QJsonObject{{"model", model}, {"max_tokens", s.maxTokens},
                               {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt}}}}};
            break;
        case AiProvider::OpenAI: {
            path = QStringLiteral("/chat/completions");
            body = QJsonObject{{"model", model}, {"messages", QJsonArray{QJsonObject{{"role", "user"}, {"content", prompt}}}},
                               {"response_format", QJsonObject{{"type", "json_object"}}}};
            // La API de OpenAI pide `max_completion_tokens` (los modelos de razonamiento rechazan el otro);
            // los servidores compatibles siguen entendiendo `max_tokens`.
            const bool official = s.baseUrl() == AiSettings::defaultBaseUrl(AiProvider::OpenAI);
            body.insert(official ? QStringLiteral("max_completion_tokens") : QStringLiteral("max_tokens"), s.maxTokens);
            break;
        }
        case AiProvider::Gemini:
            path = QStringLiteral("/models/%1:generateContent").arg(QString::fromLatin1(QUrl::toPercentEncoding(geminiModel(model))));
            body = QJsonObject{{"contents", QJsonArray{QJsonObject{{"role", "user"}, {"parts", QJsonArray{QJsonObject{{"text", prompt}}}}}}},
                               {"generationConfig", QJsonObject{{"maxOutputTokens", s.maxTokens}, {"responseMimeType", "application/json"}}}};
            break;
    }
    postJson(request(s, path, completionTimeoutMs), QJsonDocument(body), [s, model, done](const Response& r) {
        AiCompletion out;
        out.model = model;
        if (!r.ok) {
            out.error = failureOf(s, r, true);
            out.retryable = r.retryable || r.status == 429 || r.status == 529;
            done(out);
            return;
        }
        const QJsonObject o = r.json.object();
        switch (s.provider) {
            case AiProvider::Anthropic:
                for (const auto& block : o.value(QStringLiteral("content")).toArray())
                    if (block.toObject().value(QStringLiteral("type")).toString() == QLatin1String("text"))
                        out.text += block.toObject().value(QStringLiteral("text")).toString();
                out.truncated = o.value(QStringLiteral("stop_reason")).toString() == QLatin1String("max_tokens");
                out.model = o.value(QStringLiteral("model")).toString(model);
                break;
            case AiProvider::OpenAI: {
                const QJsonObject choice = o.value(QStringLiteral("choices")).toArray().first().toObject();
                out.text = choice.value(QStringLiteral("message")).toObject().value(QStringLiteral("content")).toString();
                out.truncated = choice.value(QStringLiteral("finish_reason")).toString() == QLatin1String("length");
                out.model = o.value(QStringLiteral("model")).toString(model);
                if (out.text.isEmpty()) {
                    const QString refusal = choice.value(QStringLiteral("message")).toObject().value(QStringLiteral("refusal")).toString();
                    if (!refusal.isEmpty()) out.error = tr("OpenAI no respondió: %1").arg(refusal);
                }
                break;
            }
            case AiProvider::Gemini: {
                const QJsonObject candidate = o.value(QStringLiteral("candidates")).toArray().first().toObject();
                for (const auto& part : candidate.value(QStringLiteral("content")).toObject().value(QStringLiteral("parts")).toArray())
                    out.text += part.toObject().value(QStringLiteral("text")).toString();
                const QString finish = candidate.value(QStringLiteral("finishReason")).toString();
                out.truncated = finish == QLatin1String("MAX_TOKENS");
                out.model = o.value(QStringLiteral("modelVersion")).toString(model);
                const QString blocked = o.value(QStringLiteral("promptFeedback")).toObject().value(QStringLiteral("blockReason")).toString();
                if (out.text.isEmpty() && !blocked.isEmpty()) out.error = tr("Gemini bloqueó el prompt (%1)").arg(blocked);
                else if (out.text.isEmpty() && !finish.isEmpty() && finish != QLatin1String("STOP"))
                    out.error = tr("Gemini no respondió (%1)").arg(finish);
                break;
            }
        }
        out.ok = !out.text.trimmed().isEmpty();
        if (!out.ok && out.error.isEmpty())
            out.error = out.truncated ? tr("La respuesta se cortó antes de empezar: sube el tope de tokens en Ajustes")
                                      : tr("%1 respondió sin texto").arg(label(s.provider));
        done(out);
    });
}

void AiClient::listModels(const AiSettings& s, std::function<void(const AiModelList&)> done) {
    if (!s.isConfigured()) {
        done(AiModelList{false, {}, tr("Indica la clave de API de %1").arg(label(s.provider))});
        return;
    }
    QString path;
    switch (s.provider) {
        case AiProvider::Anthropic: path = QStringLiteral("/v1/models?limit=100"); break;
        case AiProvider::OpenAI: path = QStringLiteral("/models"); break;
        case AiProvider::Gemini: path = QStringLiteral("/models?pageSize=200"); break;
    }
    get(request(s, path, kListTimeoutMs), [s, done](const Response& r) {
        AiModelList out;
        if (!r.ok) {
            out.error = failureOf(s, r, false);
            done(out);
            return;
        }
        const QJsonObject o = r.json.object();
        if (s.provider == AiProvider::Gemini) {
            for (const auto& v : o.value(QStringLiteral("models")).toArray()) {
                const QJsonObject m = v.toObject();
                const QJsonArray methods = m.value(QStringLiteral("supportedGenerationMethods")).toArray();
                if (methods.contains(QStringLiteral("generateContent"))) out.models << geminiModel(m.value(QStringLiteral("name")).toString());
            }
        } else {
            const bool official = s.provider == AiProvider::OpenAI && s.baseUrl() == AiSettings::defaultBaseUrl(AiProvider::OpenAI);
            for (const auto& v : o.value(QStringLiteral("data")).toArray()) {
                const QString id = v.toObject().value(QStringLiteral("id")).toString();
                if (!id.isEmpty() && (!official || isChatModel(id))) out.models << id;
            }
        }
        out.models.removeDuplicates();
        std::sort(out.models.begin(), out.models.end());
        out.ok = true;
        done(out);
    });
}

} // namespace qaflow
