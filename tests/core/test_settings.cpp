// TrackerSettings y CaptureSettings (core/models/Settings.h).

#include "core/models/Settings.h"

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
};

QTEST_APPLESS_MAIN(SettingsTest)
#include "test_settings.moc"
