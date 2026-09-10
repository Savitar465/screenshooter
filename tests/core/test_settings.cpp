// TrackerSettings y CaptureSettings (core/models/Settings.h).

#include "core/models/Settings.h"

#include <QUrl>
#include <QtTest>

using namespace qaflow;

class SettingsTest : public QObject {
    Q_OBJECT
private slots:
    void trackerKindRoundTripsAndDefaultsToJira() {
        for (auto k : {TrackerKind::Jira, TrackerKind::GitHub, TrackerKind::GitLab, TrackerKind::AzureDevOps})
            QCOMPARE(static_cast<int>(trackerKindFromString(toString(k))), static_cast<int>(k));
        QCOMPARE(static_cast<int>(trackerKindFromString(QStringLiteral("AzureDevOps"))), static_cast<int>(TrackerKind::AzureDevOps));
        QCOMPARE(static_cast<int>(trackerKindFromString(QStringLiteral("???"))), static_cast<int>(TrackerKind::Jira));
    }

    void baseUrlStripsTrailingSlashes() {
        TrackerSettings t;
        t.url = QStringLiteral("https://acme.atlassian.net///");
        QCOMPARE(t.baseUrl(), QStringLiteral("https://acme.atlassian.net"));
    }

    // El ciclo de Zephyr no tiene una página con id estable en Jira Server: el enlace es la búsqueda
    // de ejecuciones (ZQL) del proyecto filtrada por el nombre del ciclo, con la consulta en el fragmento.
    void zephyrCycleUrlIsTheExecutionSearchOfThatCycle() {
        TrackerSettings t;
        t.kind = TrackerKind::Jira;
        t.url = QStringLiteral("https://jira.acme.com/");
        t.project = QStringLiteral("SHOP");
        const QString url = t.zephyrCycleUrl(QStringLiteral("Regresión Sprint 14 · 12/05/2026"));
        QVERIFY2(url.startsWith(QStringLiteral("https://jira.acme.com/secure/enav/#?query=")), qPrintable(url));
        const QString zql = QUrl::fromPercentEncoding(url.mid(url.indexOf(QStringLiteral("query=")) + 6).toLatin1());
        QCOMPARE(zql, QStringLiteral("project = \"SHOP\" AND cycleName = \"Regresión Sprint 14 · 12/05/2026\""));
        QVERIFY(!url.contains(QLatin1Char(' ')));   // todo codificado

        QCOMPARE(t.zephyrCycleUrl(QStringLiteral("Ciclo \"beta\"")), QString(QStringLiteral("https://jira.acme.com/secure/enav/#?query=")
                     + QString::fromLatin1(QUrl::toPercentEncoding(QStringLiteral("project = \"SHOP\" AND cycleName = \"Ciclo \\\"beta\\\"\"")))));
        t.project.clear();
        QVERIFY(t.zephyrCycleUrl(QStringLiteral("Ciclo")).contains(QStringLiteral("cycleName")));
        QVERIFY(!t.zephyrCycleUrl(QStringLiteral("Ciclo")).contains(QStringLiteral("project")));
        QVERIFY(t.zephyrCycleUrl(QString()).isEmpty());
        t.kind = TrackerKind::GitHub;
        QVERIFY(t.zephyrCycleUrl(QStringLiteral("Ciclo")).isEmpty());   // Zephyr es de Jira
        t.kind = TrackerKind::Jira;
        t.url.clear();
        QVERIFY(t.zephyrCycleUrl(QStringLiteral("Ciclo")).isEmpty());
    }

    void issueUrlPerTracker() {
        TrackerSettings t;
        t.url = QStringLiteral("https://acme.atlassian.net/"); t.project = QStringLiteral("SHOP");
        QCOMPARE(t.issueUrl(QStringLiteral("SHOP-143")), QStringLiteral("https://acme.atlassian.net/browse/SHOP-143"));

        t.kind = TrackerKind::GitHub; t.url = QStringLiteral("https://api.github.com"); t.project = QStringLiteral("acme/tienda");
        QCOMPARE(t.issueUrl(QStringLiteral("#12")), QStringLiteral("https://github.com/acme/tienda/issues/12"));
        t.url = QStringLiteral("https://ghe.acme.com/api/v3");
        QCOMPARE(t.issueUrl(QStringLiteral("#12")), QStringLiteral("https://ghe.acme.com/acme/tienda/issues/12"));

        t.kind = TrackerKind::GitLab; t.url = QStringLiteral("https://gitlab.com");
        QCOMPARE(t.issueUrl(QStringLiteral("#7")), QStringLiteral("https://gitlab.com/acme/tienda/-/issues/7"));

        t.kind = TrackerKind::AzureDevOps; t.url = QStringLiteral("https://dev.azure.com/acme"); t.project = QStringLiteral("Tienda");
        QCOMPARE(t.issueUrl(QStringLiteral("4711")), QStringLiteral("https://dev.azure.com/acme/Tienda/_workitems/edit/4711"));
    }

    void projectLabelsDescribeEachTracker() {
        TrackerSettings t;
        QVERIFY(t.projectLabel().contains(QStringLiteral("Clave")));
        t.kind = TrackerKind::GitHub;
        QVERIFY(t.projectLabel().contains(QStringLiteral("owner/repo")));
        QCOMPARE(t.defaultUrl(), QStringLiteral("https://api.github.com"));
    }

    void captureExtensionNormalisesFormat() {
        CaptureSettings c;
        QCOMPARE(c.extension(), QStringLiteral("png"));
        c.format = QStringLiteral("JPG");
        QCOMPARE(c.extension(), QStringLiteral("jpg"));
        c.format = QStringLiteral("WebP");
        QCOMPARE(c.extension(), QStringLiteral("webp"));
        for (auto m : {CaptureMode::FullScreen, CaptureMode::ActiveWindow, CaptureMode::Region}) QCOMPARE(static_cast<int>(captureModeFromString(toString(m))), static_cast<int>(m));
    }

    void captureClampKeepsValuesInRange() {
        CaptureSettings c;
        c.delaySecs = -3; c.gifFps = 99; c.gifMaxSecs = 1; c.shortcut = QStringLiteral("  "); c.recordShortcut.clear();
        c.clamp();
        QCOMPARE(c.delaySecs, 0);
        QCOMPARE(c.gifFps, 20);
        QCOMPARE(c.gifMaxSecs, 5);
        QCOMPARE(c.shortcut, QStringLiteral("Ctrl+Shift+S"));
        QCOMPARE(c.recordShortcut, QStringLiteral("Ctrl+Shift+G"));
        c.delaySecs = 5; c.gifFps = 12; c.gifMaxSecs = 60;
        c.clamp();
        QCOMPARE(c.delaySecs, 5);
        QCOMPARE(c.gifFps, 12);
        QCOMPARE(c.gifMaxSecs, 60);
        // Valores por defecto de las opciones nuevas.
        const CaptureSettings d;
        QVERIFY(d.globalShortcut);
        QVERIFY(!d.openEditor);
        QVERIFY(!d.copyToClipboard);
    }
};

QTEST_APPLESS_MAIN(SettingsTest)
#include "test_settings.moc"
