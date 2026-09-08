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
