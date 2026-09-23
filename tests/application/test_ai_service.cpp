// AiService (application/AiService.h): generación y prueba de conexión con la IA de los ajustes.

#include "application/AiService.h"
#include "support/MemoryRepositories.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::MemorySecretStore;
using qaflow::testing::MemorySettingsRepository;

namespace {
class FakeAiClient : public IAiClient {
public:
    AiModelList models{true, {QStringLiteral("claude-a"), QStringLiteral("claude-sonnet-5")}, {}};
    QList<AiSettings> used;
    QStringList prompts;
    void complete(const AiSettings& s, const QString& prompt, std::function<void(const AiCompletion&)> done) override {
        used << s;
        prompts << prompt;
        done(AiCompletion{true, QStringLiteral("{\"cases\":[]}"), false, s.model(), {}, false});
    }
    void listModels(const AiSettings& s, std::function<void(const AiModelList&)> done) override {
        used << s;
        done(models);
    }
};

struct Fixture {
    std::shared_ptr<MemorySettingsRepository> repo = std::make_shared<MemorySettingsRepository>();
    SettingsStore settings{repo, std::make_shared<MemorySecretStore>()};
    std::shared_ptr<FakeAiClient> client = std::make_shared<FakeAiClient>();
    AiService service{client, settings};
    Fixture() { settings.load(); }
};
} // namespace

class AiServiceTest : public QObject {
    Q_OBJECT
private slots:
    void generatesWithTheChosenProvider() {
        Fixture f;
        QVERIFY(!f.service.isConfigured());
        f.settings.updateAi([](AiSettings& a) {
            a.provider = AiProvider::Gemini;
            a.of(AiProvider::Gemini).apiKey = QStringLiteral("AIza");
        });
        QVERIFY(f.service.isConfigured());
        QCOMPARE(f.service.destination(), QStringLiteral("Google (Gemini) · ") + AiSettings::defaultModel(AiProvider::Gemini));
        AiCompletion out;
        f.service.generate(QStringLiteral("prompt revisado"), [&out](const AiCompletion& r) { out = r; });
        QVERIFY(out.ok);
        QCOMPARE(f.client->prompts, QStringList{QStringLiteral("prompt revisado")});
        QCOMPARE(toString(f.client->used.last().provider), toString(AiProvider::Gemini));
    }

    void testingTheConnectionMarksItAndWarnsAboutAMissingModel() {
        Fixture f;
        f.settings.updateAi([](AiSettings& a) {
            a.of(AiProvider::Anthropic).apiKey = QStringLiteral("sk-ant");
            a.of(AiProvider::Anthropic).model = QStringLiteral("claude-viejo");
        });
        ConnectionResult out;
        f.service.testConnection([&out](const ConnectionResult& r) { out = r; });
        QVERIFY(out.ok);
        QVERIFY(out.displayName.contains(QStringLiteral("2 modelos")));
        QVERIFY(out.displayName.contains(QStringLiteral("«claude-viejo» no está")));
        QVERIFY(f.settings.ai().active().connected);

        f.client->models = AiModelList{false, {}, QStringLiteral("rechazó la clave")};
        f.service.testConnection([&out](const ConnectionResult& r) { out = r; });
        QVERIFY(!out.ok);
        QCOMPARE(out.error, QStringLiteral("rechazó la clave"));
        QVERIFY(!f.settings.ai().active().connected);
    }
};

QTEST_GUILESS_MAIN(AiServiceTest)
#include "test_ai_service.moc"
