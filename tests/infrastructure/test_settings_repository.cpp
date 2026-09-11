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

    void onlyJiraCodeIsScopedAndLegacyProjectCodesAreRetained() {
        QSettings raw;
        raw.setValue(QStringLiteral("tracker/url"), QStringLiteral("https://shared.example"));
        raw.setValue(QStringLiteral("tracker/project"), QStringLiteral("MAIN"));
        raw.setValue(QStringLiteral("projects/a/tracker/project"), QStringLiteral("A"));
        raw.setValue(QStringLiteral("projects/a/tracker/url"), QStringLiteral("https://old-private.example"));
        raw.setValue(QStringLiteral("capture/folder"), QStringLiteral("/shared"));
        raw.setValue(QStringLiteral("projects/a/capture/folder"), QStringLiteral("/old-private"));
        QSettingsRepository a(QStringLiteral("a")), b(QStringLiteral("b")), main(QStringLiteral("default"));
        QCOMPARE(a.loadTracker().project, QStringLiteral("A"));
        QCOMPARE(main.loadTracker().project, QStringLiteral("MAIN"));
        QVERIFY(b.loadTracker().project.isEmpty());
        QCOMPARE(a.loadTracker().url, QStringLiteral("https://shared.example"));
        QCOMPARE(a.loadCapture().folder, QStringLiteral("/shared"));
        auto t = b.loadTracker();
        t.project = QStringLiteral("B"); t.url = QStringLiteral("https://updated.example"); t.zephyr = true;
        b.saveTracker(t);
        QCOMPARE(a.loadTracker().url, t.url);
        QVERIFY(a.loadTracker().zephyr);
        QCOMPARE(a.loadTracker().project, QStringLiteral("A"));
        QCOMPARE(main.loadTracker().project, QStringLiteral("MAIN"));
        t.kind = TrackerKind::GitHub; t.project = QStringLiteral("org/repo");
        b.saveTracker(t);
        QCOMPARE(a.loadTracker().project, QStringLiteral("org/repo"));
        t.kind = TrackerKind::Jira;
        b.saveTracker(t);
        QCOMPARE(b.loadTracker().project, QStringLiteral("B"));
        QCOMPARE(a.loadTracker().project, QStringLiteral("A"));
    }

    void runShortcutsRoundTrip() {
        QSettingsRepository repo;
        QCOMPARE(repo.loadRunShortcuts().passAndNext, RunShortcuts{}.passAndNext);   // sin fichero: los de fábrica
        RunShortcuts r;
        r.passAndNext = QStringLiteral("Ctrl+Alt+1");
        r.failAndNext = QStringLiteral("Ctrl+Alt+2");
        r.previous = QStringLiteral("Ctrl+Alt+3");
        repo.saveRunShortcuts(r);
        const RunShortcuts loaded = repo.loadRunShortcuts();
        QCOMPARE(loaded.passAndNext, r.passAndNext);
        QCOMPARE(loaded.failAndNext, r.failAndNext);
        QCOMPARE(loaded.previous, r.previous);
    }

    void trackerRoundTripNeverWritesTokenUnlessGiven() {
        QSettingsRepository repo;
        TrackerSettings t;
        t.kind = TrackerKind::GitLab; t.url = QStringLiteral("https://gitlab.com"); t.project = QStringLiteral("acme/shop");
        t.user = QStringLiteral("x@y"); t.connected = true;
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

    void jiraAuthRoundTripsAndMigratesTheOldEmailKey() {
        {
            QSettings s;   // ajustes de una versión anterior: sólo existía "email", que implicaba Jira Cloud
            s.setValue(QStringLiteral("tracker/url"), QStringLiteral("https://acme.atlassian.net"));
            s.setValue(QStringLiteral("tracker/email"), QStringLiteral("qa@acme.com"));
        }
        QSettingsRepository repo;
        TrackerSettings t = repo.loadTracker();
        QCOMPARE(t.user, QStringLiteral("qa@acme.com"));
        QCOMPARE(static_cast<int>(t.jiraAuth), static_cast<int>(JiraAuth::CloudToken));

        // Al pasar a Jira Server con usuario y contraseña, el modo se guarda y la clave antigua desaparece.
        t.jiraAuth = JiraAuth::ServerBasic;
        t.url = QStringLiteral("https://jira.acme.com");
        t.user = QStringLiteral("aperez");
        repo.saveTracker(t);
        QVERIFY(!QSettings().contains(QStringLiteral("tracker/email")));
        const TrackerSettings loaded = repo.loadTracker();
        QCOMPARE(static_cast<int>(loaded.jiraAuth), static_cast<int>(JiraAuth::ServerBasic));
        QCOMPARE(loaded.user, QStringLiteral("aperez"));
        QVERIFY(loaded.needsUser());
        QVERIFY(!loaded.usesAccountId());
    }

    void trackerWithoutUserOrModeIsReadAsAServerToken() {
        {
            QSettings s;   // versión anterior sin correo: el token era un PAT de Jira Server
            s.setValue(QStringLiteral("tracker/url"), QStringLiteral("https://jira.acme.com"));
        }
        const TrackerSettings t = QSettingsRepository().loadTracker();
        QCOMPARE(static_cast<int>(t.jiraAuth), static_cast<int>(JiraAuth::ServerToken));
        QVERIFY(!t.needsUser());
    }

    void captureAndAppSettingsRoundTrip() {
        QSettingsRepository repo;
        CaptureSettings c;
        c.shortcut = QStringLiteral("Ctrl+Alt+S"); c.format = QStringLiteral("JPG"); c.mode = CaptureMode::Region; c.folder = QStringLiteral("/tmp/caps");
        c.recordShortcut = QStringLiteral("F8"); c.delaySecs = 5; c.globalShortcut = false; c.openEditor = true; c.copyToClipboard = true;
        c.gifFps = 15; c.gifMaxSecs = 45;
        repo.saveCapture(c);
        const CaptureSettings lc = repo.loadCapture();
        QCOMPARE(lc.shortcut, c.shortcut);
        QCOMPARE(lc.format, c.format);
        QCOMPARE(static_cast<int>(lc.mode), static_cast<int>(CaptureMode::Region));
        QCOMPARE(lc.folder, c.folder);
        QCOMPARE(lc.recordShortcut, QStringLiteral("F8"));
        QCOMPARE(lc.delaySecs, 5);
        QVERIFY(!lc.globalShortcut);
        QVERIFY(lc.openEditor);
        QVERIFY(lc.copyToClipboard);
        QCOMPARE(lc.gifFps, 15);
        QCOMPARE(lc.gifMaxSecs, 45);
        // Valores fuera de rango editados a mano en el fichero se corrigen al cargar.
        QSettings().setValue(QStringLiteral("capture/gifFps"), 500);
        QCOMPARE(repo.loadCapture().gifFps, 20);

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
