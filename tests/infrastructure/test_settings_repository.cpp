// QSettingsRepository y PlainSettingsSecretStore sobre un fichero de ajustes temporal
// (QSettings redirigido a un directorio propio del test).

#include "infrastructure/persistence/QSettingsRepository.h"
#include "infrastructure/secrets/SecretStores.h"

#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace qaflow;

class SettingsRepositoryTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("QAflowTest"));
        QCoreApplication::setApplicationName(QStringLiteral("QAflowTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }
    void init() { QSettings().clear(); }

    void trackerRoundTripNeverWritesTokenUnlessGiven() {
        QSettingsRepository repo;
        TrackerSettings t;
        t.kind = TrackerKind::GitLab; t.url = QStringLiteral("https://gitlab.com"); t.project = QStringLiteral("acme/shop");
        t.email = QStringLiteral("x@y"); t.connected = true;
        repo.saveTracker(t);   // sin token: no aparece en el fichero
        QVERIFY(!QSettings().contains(QStringLiteral("tracker/token")));
        const TrackerSettings loaded = repo.loadTracker();
        QCOMPARE(static_cast<int>(loaded.kind), static_cast<int>(TrackerKind::GitLab));
        QCOMPARE(loaded.url, t.url);
        QCOMPARE(loaded.project, t.project);
        QVERIFY(loaded.connected);
        QVERIFY(loaded.token.isEmpty());

        t.token = QStringLiteral("plain");   // sin llavero: SettingsStore lo manda en claro
        repo.saveTracker(t);
        QCOMPARE(repo.loadTracker().token, QStringLiteral("plain"));
        t.token.clear();
        repo.saveTracker(t);
        QVERIFY(!QSettings().contains(QStringLiteral("tracker/token")));
    }

    void legacyJiraGroupIsReadAndItsTokenRemovedOnSave() {
        {
            QSettings s;
            s.setValue(QStringLiteral("jira/url"), QStringLiteral("https://old.atlassian.net"));
            s.setValue(QStringLiteral("jira/project"), QStringLiteral("OLD"));
            s.setValue(QStringLiteral("jira/token"), QStringLiteral("legacy-token"));
        }
        QSettingsRepository repo;
        TrackerSettings t = repo.loadTracker();
        QCOMPARE(static_cast<int>(t.kind), static_cast<int>(TrackerKind::Jira));
        QCOMPARE(t.url, QStringLiteral("https://old.atlassian.net"));
        QCOMPARE(t.project, QStringLiteral("OLD"));
        QCOMPARE(t.token, QStringLiteral("legacy-token"));   // se entrega una vez para migrarlo
        t.token.clear();
        repo.saveTracker(t);
        QVERIFY(!QSettings().contains(QStringLiteral("jira/token")));
        QCOMPARE(repo.loadTracker().url, QStringLiteral("https://old.atlassian.net"));   // ahora desde "tracker"
    }

    void captureAndAppSettingsRoundTrip() {
        QSettingsRepository repo;
        CaptureSettings c;
        c.shortcut = QStringLiteral("Ctrl+Alt+S"); c.format = QStringLiteral("JPG"); c.mode = CaptureMode::Region; c.folder = QStringLiteral("/tmp/caps");
        repo.saveCapture(c);
        const CaptureSettings lc = repo.loadCapture();
        QCOMPARE(lc.shortcut, c.shortcut);
        QCOMPARE(lc.format, c.format);
        QCOMPARE(static_cast<int>(lc.mode), static_cast<int>(CaptureMode::Region));
        QCOMPARE(lc.folder, c.folder);

        QCOMPARE(static_cast<int>(repo.loadApp().theme), static_cast<int>(AppTheme::Dark));   // valores por defecto
        QCOMPARE(static_cast<int>(repo.loadApp().language), static_cast<int>(AppLanguage::System));
        AppSettings a;
        a.language = AppLanguage::English; a.theme = AppTheme::Light; a.closeToTray = true;
        repo.saveApp(a);
        const AppSettings la = repo.loadApp();
        QCOMPARE(static_cast<int>(la.language), static_cast<int>(AppLanguage::English));
        QCOMPARE(static_cast<int>(la.theme), static_cast<int>(AppTheme::Light));
        QVERIFY(la.closeToTray);
        QCOMPARE(QSettings().value(QStringLiteral("app/theme")).toString(), QStringLiteral("light"));
    }

    void plainSecretStoreKeepsValuesInSettings() {
        PlainSettingsSecretStore store;
        QVERIFY(!store.isSecure());
        QVERIFY(!store.read(QStringLiteral("tracker/jira/token")));
        QVERIFY(store.write(QStringLiteral("tracker/jira/token"), QStringLiteral("abc")));
        QCOMPARE(store.read(QStringLiteral("tracker/jira/token")).value_or(QString()), QStringLiteral("abc"));
        QCOMPARE(QSettings().value(QStringLiteral("secrets/tracker/jira/token")).toString(), QStringLiteral("abc"));
        store.remove(QStringLiteral("tracker/jira/token"));
        QVERIFY(!store.read(QStringLiteral("tracker/jira/token")));
        QVERIFY(store.description().contains(m_dir.path()));
    }

    void makeSecretStoreAlwaysReturnsAWorkingStore() {
        auto store = makeSecretStore();
        QVERIFY(store);
        QVERIFY(store->write(QStringLiteral("qaflow/test"), QStringLiteral("v")));
        QCOMPARE(store->read(QStringLiteral("qaflow/test")).value_or(QString()), QStringLiteral("v"));
        store->remove(QStringLiteral("qaflow/test"));
        QVERIFY(!store->description().isEmpty());
    }
};

QTEST_APPLESS_MAIN(SettingsRepositoryTest)
#include "test_settings_repository.moc"
