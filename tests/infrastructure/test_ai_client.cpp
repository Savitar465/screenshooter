// AiClient (infrastructure/ai/AiClient.h): las API de Anthropic, OpenAI y Gemini contra un servidor falso.

#include "infrastructure/ai/AiClient.h"
#include "support/FakeHttpServer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

using namespace qaflow;
using qaflow::testing::FakeHttpServer;
using qaflow::testing::HttpRequest;
using qaflow::testing::HttpResponse;

namespace {
AiSettings settingsFor(AiProvider provider, const FakeHttpServer& server, const QString& path = QString()) {
    AiSettings s;
    s.provider = provider;
    s.maxTokens = 4096;
    s.of(provider).apiKey = QStringLiteral("clave-secreta");
    s.of(provider).model = QStringLiteral("modelo-x");
    s.of(provider).baseUrl = server.baseUrl() + path;
    return s;
}

AiCompletion completeSync(AiClient& client, const AiSettings& s, const QString& prompt = QStringLiteral("Genera casos en JSON")) {
    AiCompletion out;
    bool done = false;
    client.complete(s, prompt, [&](const AiCompletion& r) { out = r; done = true; });
    if (!QTest::qWaitFor([&] { return done; }, 10000)) qFatal("Sin respuesta");
    return out;
}

AiModelList modelsSync(AiClient& client, const AiSettings& s) {
    AiModelList out;
    bool done = false;
    client.listModels(s, [&](const AiModelList& r) { out = r; done = true; });
    if (!QTest::qWaitFor([&] { return done; }, 10000)) qFatal("Sin respuesta");
    return out;
}

QJsonObject bodyOf(const HttpRequest& r) { return QJsonDocument::fromJson(r.body).object(); }
} // namespace

class AiClientTest : public QObject {
    Q_OBJECT
private slots:
    // ---- Anthropic ---------------------------------------------------------------------

    void anthropicSendsTheMessagesRequestAndJoinsTheText() {
        FakeHttpServer server;
        HttpRequest seen;
        server.route("POST", "/v1/messages", [&seen](const HttpRequest& r) {
            seen = r;
            return HttpResponse::json(200, "{\"model\":\"modelo-x-2026\",\"stop_reason\":\"end_turn\",\"content\":["
                "                {\"type\":\"text\",\"text\":\"{\\\"cases\\\":\"},{\"type\":\"text\",\"text\":\"[]}\"}]}");
        });
        AiClient client;
        const AiCompletion r = completeSync(client, settingsFor(AiProvider::Anthropic, server));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.text, QStringLiteral("{\"cases\":[]}"));
        QCOMPARE(r.model, QStringLiteral("modelo-x-2026"));
        QVERIFY(!r.truncated);
        QCOMPARE(seen.header("x-api-key"), QByteArray("clave-secreta"));
        QCOMPARE(seen.header("anthropic-version"), QByteArray("2023-06-01"));
        const QJsonObject body = bodyOf(seen);
        QCOMPARE(body["model"].toString(), QStringLiteral("modelo-x"));
        QCOMPARE(body["max_tokens"].toInt(), 4096);
        QCOMPARE(body["messages"].toArray().first().toObject()["content"].toString(), QStringLiteral("Genera casos en JSON"));
    }

    void anthropicReportsATruncatedAnswer() {
        FakeHttpServer server;
        server.route("POST", "/v1/messages", [](const HttpRequest&) {
            return HttpResponse::json(200, "{\"stop_reason\":\"max_tokens\",\"content\":[{\"type\":\"text\",\"text\":\"{\\\"cases\\\":[{\\\"title\\\"\"}]}");
        });
        AiClient client;
        const AiCompletion r = completeSync(client, settingsFor(AiProvider::Anthropic, server));
        QVERIFY(r.ok);
        QVERIFY(r.truncated);
    }

    void errorsAreActionableAndNeverShowTheKey() {
        FakeHttpServer server;
        int status = 401;
        server.route("POST", "/v1/messages", [&status](const HttpRequest&) {
            return HttpResponse::json(status, R"({"type":"error","error":{"type":"x","message":"detalle del proveedor"}})");
        });
        AiClient client;
        const AiSettings s = settingsFor(AiProvider::Anthropic, server);
        AiCompletion r = completeSync(client, s);
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("clave")));
        QVERIFY(r.error.contains(QStringLiteral("detalle del proveedor")));
        QVERIFY(!r.error.contains(QStringLiteral("clave-secreta")));
        status = 404;
        QVERIFY(completeSync(client, s).error.contains(QStringLiteral("«modelo-x»")));
        status = 429;
        r = completeSync(client, s);
        QVERIFY(r.error.contains(QStringLiteral("límite")));
        QVERIFY(r.retryable);
    }

    void withoutKeyNothingIsSent() {
        FakeHttpServer server;
        AiSettings s = settingsFor(AiProvider::OpenAI, server);
        s.of(AiProvider::OpenAI).apiKey.clear();
        AiClient client;
        QVERIFY(!completeSync(client, s).ok);
        QVERIFY(!modelsSync(client, s).ok);
        QVERIFY(server.requests.isEmpty());
    }

    // ---- OpenAI ------------------------------------------------------------------------

    void openAiAsksForJsonWithBearerAuth() {
        FakeHttpServer server;
        HttpRequest seen;
        server.route("POST", "/v1/chat/completions", [&seen](const HttpRequest& r) {
            seen = r;
            return HttpResponse::json(200, "{\"model\":\"modelo-x\",\"choices\":[{\"finish_reason\":\"length\",\"message\":{\"content\":\"{\\\"cases\\\":[]}\"}}]}");
        });
        AiClient client;
        const AiCompletion r = completeSync(client, settingsFor(AiProvider::OpenAI, server, QStringLiteral("/v1")));
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.text, QStringLiteral("{\"cases\":[]}"));
        QVERIFY(r.truncated);
        QCOMPARE(seen.header("authorization"), QByteArray("Bearer clave-secreta"));
        const QJsonObject body = bodyOf(seen);
        QCOMPARE(body["response_format"].toObject()["type"].toString(), QStringLiteral("json_object"));
        // Un servidor compatible (no la API oficial) recibe el tope con el nombre clásico.
        QCOMPARE(body["max_tokens"].toInt(), 4096);
        QVERIFY(!body.contains(QStringLiteral("max_completion_tokens")));
    }

    void openAiRefusalIsAnError() {
        FakeHttpServer server;
        server.route("POST", "/chat/completions", [](const HttpRequest&) {
            return HttpResponse::json(200, R"({"choices":[{"finish_reason":"stop","message":{"content":null,"refusal":"No puedo"}}]})");
        });
        AiClient client;
        const AiCompletion r = completeSync(client, settingsFor(AiProvider::OpenAI, server));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("No puedo")));
    }

    void openAiListsOnlyTextModels() {
        FakeHttpServer server;
        server.route("GET", "/models", [](const HttpRequest&) {
            return HttpResponse::json(200, R"({"data":[{"id":"gpt-b"},{"id":"text-embedding-3"},{"id":"gpt-a"},{"id":"whisper-1"}]})");
        });
        AiClient client;
        const AiModelList compatible = modelsSync(client, settingsFor(AiProvider::OpenAI, server));
        QVERIFY(compatible.ok);
        // Contra un servidor compatible no se filtra: sus modelos no siguen los nombres de OpenAI.
        QCOMPARE(compatible.models.size(), 4);
        QCOMPARE(compatible.models.first(), QStringLiteral("gpt-a"));
    }

    // ---- Gemini ------------------------------------------------------------------------

    void geminiUsesGenerateContentWithTheModelInThePath() {
        FakeHttpServer server;
        HttpRequest seen;
        server.route("POST", "/v1beta/models/modelo-x:generateContent", [&seen](const HttpRequest& r) {
            seen = r;
            return HttpResponse::json(200, "{\"modelVersion\":\"modelo-x-001\",\"candidates\":[{\"finishReason\":\"STOP\","
                "                \"content\":{\"parts\":[{\"text\":\"{\\\"cases\\\"\"},{\"text\":\":[]}\"}]}}]}");
        });
        AiClient client;
        AiSettings s = settingsFor(AiProvider::Gemini, server, QStringLiteral("/v1beta"));
        s.of(AiProvider::Gemini).model = QStringLiteral("models/modelo-x");   // como lo lista la API
        const AiCompletion r = completeSync(client, s);
        QVERIFY2(r.ok, qPrintable(r.error));
        QCOMPARE(r.text, QStringLiteral("{\"cases\":[]}"));
        QCOMPARE(r.model, QStringLiteral("modelo-x-001"));
        QCOMPARE(seen.header("x-goog-api-key"), QByteArray("clave-secreta"));
        QVERIFY(!seen.path.contains("key="));   // la clave no va en la URL
        const QJsonObject config = bodyOf(seen)["generationConfig"].toObject();
        QCOMPARE(config["responseMimeType"].toString(), QStringLiteral("application/json"));
        QCOMPARE(config["maxOutputTokens"].toInt(), 4096);
    }

    void geminiBlockedPromptIsAnError() {
        FakeHttpServer server;
        server.route("POST", "/models/modelo-x:generateContent", [](const HttpRequest&) {
            return HttpResponse::json(200, R"({"promptFeedback":{"blockReason":"SAFETY"}})");
        });
        AiClient client;
        const AiCompletion r = completeSync(client, settingsFor(AiProvider::Gemini, server));
        QVERIFY(!r.ok);
        QVERIFY(r.error.contains(QStringLiteral("SAFETY")));
    }

    void geminiListsModelsThatGenerateContent() {
        FakeHttpServer server;
        server.route("GET", "/models", [](const HttpRequest&) {
            return HttpResponse::json(200, R"({"models":[
                {"name":"models/gemini-b","supportedGenerationMethods":["generateContent","countTokens"]},
                {"name":"models/embedding-001","supportedGenerationMethods":["embedContent"]},
                {"name":"models/gemini-a","supportedGenerationMethods":["generateContent"]}]})");
        });
        AiClient client;
        const AiModelList r = modelsSync(client, settingsFor(AiProvider::Gemini, server));
        QVERIFY(r.ok);
        QCOMPARE(r.models, (QStringList{QStringLiteral("gemini-a"), QStringLiteral("gemini-b")}));
    }

    void anthropicListsModels() {
        FakeHttpServer server;
        server.route("GET", "/v1/models", [](const HttpRequest&) {
            return HttpResponse::json(200, R"({"data":[{"id":"claude-b"},{"id":"claude-a"}]})");
        });
        AiClient client;
        const AiModelList r = modelsSync(client, settingsFor(AiProvider::Anthropic, server));
        QCOMPARE(r.models, (QStringList{QStringLiteral("claude-a"), QStringLiteral("claude-b")}));
    }
};

QTEST_GUILESS_MAIN(AiClientTest)
#include "test_ai_client.moc"
