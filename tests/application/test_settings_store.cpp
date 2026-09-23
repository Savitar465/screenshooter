// SettingsStore (application/SettingsStore.h): token en el llavero, migración y ajustes de captura.

#include "support/MemoryRepositories.h"

#include "application/SettingsStore.h"

#include <QtTest>

using namespace qaflow;
using qaflow::testing::MemorySecretStore;
using qaflow::testing::MemorySettingsRepository;

class SettingsStoreTest : public QObject {
    Q_OBJECT
private slots:
    void tokenGoesToSecretStoreNotToRepository() {
        auto repo = std::make_shared<MemorySettingsRepository>();
        auto secrets = std::make_shared<MemorySecretStore>();
        SettingsStore store(repo, secrets);
        store.load();
        store.updateTracker([](TrackerSettings& s) { s.token = QStringLiteral("abc123"); s.project = QStringLiteral("SHOP"); });
        QCOMPARE(store.tracker().token, QStringLiteral("abc123"));
        QVERIFY(repo->tracker.token.isEmpty());                                           // nunca en el fichero
        QCOMPARE(secrets->values.value(QStringLiteral("tracker/jira/token")), QStringLiteral("abc123"));
        QCOMPARE(repo->tracker.project, QStringLiteral("SHOP"));
        store.updateTracker([](TrackerSettings& s) { s.token.clear(); });
        QVERIFY(!secrets->values.contains(QStringLiteral("tracker/jira/token")));        // borrado al vaciar
    }

    void runShortcutsArePersistedAndAnnounced() {
        auto repo = std::make_shared<MemorySettingsRepository>();
        SettingsStore store(repo);
        store.load();
        QSignalSpy spy(&store, &SettingsStore::runShortcutsChanged);
        store.updateRunShortcuts([](RunShortcuts& r) { r.passAndNext = QStringLiteral("F8"); });
        QCOMPARE(spy.size(), 1);
        QCOMPARE(store.runShortcuts().passAndNext, QStringLiteral("F8"));
        QCOMPARE(repo->runShortcuts.passAndNext, QStringLiteral("F8"));
        QCOMPARE(store.runShortcuts().previous, RunShortcuts{}.previous);   // el resto no se toca
    }

    void legacyPlainTokenIsMigratedToSecretStore() {
        auto repo = std::make_shared<MemorySettingsRepository>();
        repo->tracker.token = QStringLiteral("legacy");
        auto secrets = std::make_shared<MemorySecretStore>();
        SettingsStore store(repo, secrets);
        store.load();
        QCOMPARE(store.tracker().token, QStringLiteral("legacy"));
        QCOMPARE(secrets->values.value(QStringLiteral("tracker/jira/token")), QStringLiteral("legacy"));
        QVERIFY(repo->tracker.token.isEmpty());   // el repositorio se reescribe sin el token
    }

    void eachTrackerKeepsItsOwnToken() {
        auto repo = std::make_shared<MemorySettingsRepository>();
        auto secrets = std::make_shared<MemorySecretStore>();
        SettingsStore store(repo, secrets);
        store.load();
        store.updateTracker([](TrackerSettings& s) { s.token = QStringLiteral("jira-token"); });
        store.updateTracker([](TrackerSettings& s) { s.kind = TrackerKind::GitHub; });
        QVERIFY(store.tracker().token.isEmpty());                          // GitHub aún sin token
        store.updateTracker([](TrackerSettings& s) { s.token = QStringLiteral("gh-token"); });
        store.updateTracker([](TrackerSettings& s) { s.kind = TrackerKind::Jira; });
        QCOMPARE(store.tracker().token, QStringLiteral("jira-token"));    // vuelve el de Jira
        QCOMPARE(secrets->values.size(), 2);
    }

    void withoutSecretStoreTokenStaysInRepository() {
        auto repo = std::make_shared<MemorySettingsRepository>();
        SettingsStore store(repo, nullptr);
        store.load();
        store.updateTracker([](TrackerSettings& s) { s.token = QStringLiteral("plain"); });
        QCOMPARE(repo->tracker.token, QStringLiteral("plain"));
        QVERIFY(!store.secretsAreSecure());
    }

    // La contraseña de GESREQ va al llavero como el token del gestor; dirección y usuario, al fichero.
    void gesreqPasswordGoesToSecretStoreNotToRepository() {
        auto repo = std::make_shared<MemorySettingsRepository>();
        auto secrets = std::make_shared<MemorySecretStore>();
        SettingsStore store(repo, secrets);
        store.load();
        QSignalSpy changed(&store, &SettingsStore::requirementSourceChanged);
        store.updateRequirementSource([](RequirementSourceSettings& r) {
            r.url = QStringLiteral("http://gesreq.test:7401/greq");
            r.user = QStringLiteral("QAUSR0101");
            r.password = QStringLiteral("s3creta");
        });
        QCOMPARE(changed.count(), 1);
        QCOMPARE(store.requirementSource().password, QStringLiteral("s3creta"));
        QVERIFY(repo->requirementSource.password.isEmpty());                                  // nunca en el fichero
        QCOMPARE(repo->requirementSource.url, QStringLiteral("http://gesreq.test:7401/greq"));
        QCOMPARE(secrets->values.value(QStringLiteral("gesreq/password")), QStringLiteral("s3creta"));
        QVERIFY(!secrets->values.contains(QStringLiteral("tracker/jira/token")));            // no se mezcla con el del gestor

        SettingsStore reloaded(repo, secrets);
        reloaded.load();
        QCOMPARE(reloaded.requirementSource().user, QStringLiteral("QAUSR0101"));
        QCOMPARE(reloaded.requirementSource().password, QStringLiteral("s3creta"));

        store.updateRequirementSource([](RequirementSourceSettings& r) { r.password.clear(); });
        QVERIFY(!secrets->values.contains(QStringLiteral("gesreq/password")));               // borrada al vaciar
    }

    void plainGesreqPasswordIsMigratedToSecretStore() {
        auto repo = std::make_shared<MemorySettingsRepository>();
        repo->requirementSource.url = QStringLiteral("http://gesreq.test:7401/greq");
        repo->requirementSource.password = QStringLiteral("en-claro");
        auto secrets = std::make_shared<MemorySecretStore>();
        SettingsStore store(repo, secrets);
        store.load();
        QCOMPARE(store.requirementSource().password, QStringLiteral("en-claro"));
        QCOMPARE(secrets->values.value(QStringLiteral("gesreq/password")), QStringLiteral("en-claro"));
        QVERIFY(repo->requirementSource.password.isEmpty());   // el repositorio se reescribe sin ella
        QCOMPARE(repo->requirementSource.url, QStringLiteral("http://gesreq.test:7401/greq"));
    }

    // ---- IA ------------------------------------------------------------------------------

    void eachAiProviderKeepsItsKeyInTheSecretStore() {
        auto repo = std::make_shared<MemorySettingsRepository>();
        auto secrets = std::make_shared<MemorySecretStore>();
        SettingsStore store(repo, secrets);
        store.load();
        QSignalSpy changed(&store, &SettingsStore::aiChanged);
        store.updateAi([](AiSettings& a) { a.of(AiProvider::Anthropic).apiKey = QStringLiteral("sk-ant-1"); });
        store.updateAi([](AiSettings& a) {
            a.provider = AiProvider::Gemini;
            a.of(AiProvider::Gemini).apiKey = QStringLiteral("AIza-2");
            a.of(AiProvider::Gemini).model = QStringLiteral("gemini-x");
        });
        QCOMPARE(changed.count(), 2);
        QCOMPARE(secrets->values.value(QStringLiteral("ai/anthropic/apiKey")), QStringLiteral("sk-ant-1"));
        QCOMPARE(secrets->values.value(QStringLiteral("ai/gemini/apiKey")), QStringLiteral("AIza-2"));
        for (const auto& p : repo->ai.providers) QVERIFY(p.apiKey.isEmpty());   // nunca en el fichero
        QCOMPARE(toString(repo->ai.provider), toString(AiProvider::Gemini));
        QCOMPARE(repo->ai.of(AiProvider::Gemini).model, QStringLiteral("gemini-x"));

        SettingsStore reloaded(repo, secrets);
        reloaded.load();
        QCOMPARE(reloaded.ai().active().apiKey, QStringLiteral("AIza-2"));
        QCOMPARE(reloaded.ai().of(AiProvider::Anthropic).apiKey, QStringLiteral("sk-ant-1"));
        QCOMPARE(reloaded.ai().model(), QStringLiteral("gemini-x"));
        reloaded.updateAi([](AiSettings& a) { a.of(AiProvider::Anthropic).apiKey.clear(); });
        QVERIFY(!secrets->values.contains(QStringLiteral("ai/anthropic/apiKey")));
    }

    void plainAiKeysAreMigratedToSecretStore() {
        auto repo = std::make_shared<MemorySettingsRepository>();
        repo->ai.of(AiProvider::OpenAI).apiKey = QStringLiteral("sk-old");
        auto secrets = std::make_shared<MemorySecretStore>();
        SettingsStore store(repo, secrets);
        store.load();
        QCOMPARE(store.ai().of(AiProvider::OpenAI).apiKey, QStringLiteral("sk-old"));
        QCOMPARE(secrets->values.value(QStringLiteral("ai/openai/apiKey")), QStringLiteral("sk-old"));
        QVERIFY(repo->ai.of(AiProvider::OpenAI).apiKey.isEmpty());
    }

    void aiDefaultsAndLimits() {
        AiSettings a;
        QVERIFY(!a.isConfigured());
        QCOMPARE(a.model(), AiSettings::defaultModel(AiProvider::Anthropic));
        a.provider = AiProvider::OpenAI;
        a.of(AiProvider::OpenAI).baseUrl = QStringLiteral("http://localhost:11434/v1/");
        QCOMPARE(a.baseUrl(), QStringLiteral("http://localhost:11434/v1"));
        a.of(AiProvider::OpenAI).apiKey = QStringLiteral("  ");
        QVERIFY(!a.isConfigured());
        a.maxTokens = 5;
        a.clamp();
        QCOMPARE(a.maxTokens, 8192);
        QCOMPARE(toString(aiProviderFromString(toString(AiProvider::Gemini))), toString(AiProvider::Gemini));
    }

    void captureFolderDefaultsToHome() {
        SettingsStore store(std::make_shared<MemorySettingsRepository>());
        store.load();
        QVERIFY(store.capture().folder.endsWith(QStringLiteral("QAflow/capturas")));
        QSignalSpy spy(&store, &SettingsStore::captureChanged);
        store.updateCapture([](CaptureSettings& c) { c.format = QStringLiteral("JPG"); });
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_APPLESS_MAIN(SettingsStoreTest)
#include "test_settings_store.moc"
