// GitHubReleaseSource (infrastructure/update) contra un servidor falso y QSettingsUpdatePreferences sobre
// un fichero de ajustes temporal.

#include "infrastructure/persistence/QSettingsUpdatePreferences.h"
#include "infrastructure/update/GitHubReleaseSource.h"
#include "support/FakeHttpServer.h"

#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace qaflow;
using qaflow::testing::FakeHttpServer;
using qaflow::testing::HttpRequest;
using qaflow::testing::HttpResponse;

namespace {
UpdateCheckResult fetchSync(GitHubReleaseSource& source) {
    UpdateCheckResult out;
    bool done = false;
    source.fetchReleases([&](const UpdateCheckResult& r) { out = r; done = true; });
    if (!QTest::qWaitFor([&] { return done; }, 10000)) qFatal("Sin respuesta");
    return out;
}
} // namespace

class UpdateSourceTest : public QObject {
    Q_OBJECT
    QTemporaryDir m_dir;

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName(QStringLiteral("QAflowTest"));
        QCoreApplication::setApplicationName(QStringLiteral("QAflowTest"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }

    void readsTheReleasesAndSkipsDraftsAndTagsThatAreNotVersions() {
        FakeHttpServer server;
        HttpRequest seen;
        server.route("GET", "/repos/dueno/qaflow/releases", [&seen](const HttpRequest& r) {
            seen = r;
            return HttpResponse::json(200,
                "[{\"tag_name\":\"v1.7.0\",\"name\":\"\",\"draft\":true,\"prerelease\":false},"
                " {\"tag_name\":\"v1.6.0\",\"name\":\"QAflow 1.6\",\"body\":\"- Novedad\","
                "  \"html_url\":\"https://github.com/dueno/qaflow/releases/tag/v1.6.0\",\"draft\":false,\"prerelease\":false,"
                "  \"published_at\":\"2026-10-01T12:00:00Z\",\"assets\":["
                "    {\"name\":\"SHA256SUMS\",\"browser_download_url\":\"https://example.test/SHA256SUMS\",\"size\":120}]},"
                " {\"tag_name\":\"v1.6.1\",\"name\":\"\",\"draft\":false,\"prerelease\":true},"
                " {\"tag_name\":\"nightly\",\"draft\":false,\"prerelease\":true}]");
        });
        GitHubReleaseSource source(QStringLiteral("dueno/qaflow"), server.baseUrl());
        const UpdateCheckResult r = fetchSync(source);
        QVERIFY2(r.ok, qPrintable(r.error));
        QVERIFY(seen.path.contains("per_page="));
        QVERIFY(!seen.header("user-agent").isEmpty());
        QCOMPARE(r.releases.size(), 2);
        const UpdateRelease& stable = r.releases[0];
        QCOMPARE(stable.version.toString(), QStringLiteral("1.6.0"));
        QCOMPARE(stable.title, QStringLiteral("QAflow 1.6"));
        QCOMPARE(stable.notes, QStringLiteral("- Novedad"));
        QCOMPARE(stable.pageUrl, QUrl(QStringLiteral("https://github.com/dueno/qaflow/releases/tag/v1.6.0")));
        QCOMPARE(stable.publishedAt, QDateTime(QDate(2026, 10, 1), QTime(12, 0), Qt::UTC));
        QCOMPARE(stable.assets.size(), 1);
        QVERIFY(stable.asset(kChecksumsAsset));
        QCOMPARE(stable.asset(kChecksumsAsset)->url, QUrl(QStringLiteral("https://example.test/SHA256SUMS")));
        QCOMPARE(stable.asset(kChecksumsAsset)->size, 120);
        QVERIFY(!stable.asset(kChecksumsSignatureAsset));
        // Marcada como previa en GitHub aunque la etiqueta no lo diga: no llega al canal estable.
        QCOMPARE(r.releases[1].title, QStringLiteral("v1.6.1"));
        QVERIFY(r.releases[1].version.isPreRelease());
        QVERIFY(!newestUpdate(r.releases, *Version::parse(QStringLiteral("1.6.0")), UpdateChannel::Stable));
    }

    void reportsTheServerError() {
        FakeHttpServer server;
        server.fallback([](const HttpRequest&) { return HttpResponse::json(403, "{\"message\":\"API rate limit exceeded\"}"); });
        GitHubReleaseSource source(QStringLiteral("dueno/qaflow"), server.baseUrl());
        const UpdateCheckResult r = fetchSync(source);
        QVERIFY(!r.ok);
        QVERIFY2(r.error.contains(QStringLiteral("rate limit")), qPrintable(r.error));
    }

    void aDownloadGoesToDiskAndOnlyStaysIfItSucceeds() {
        FakeHttpServer server;
        const QByteArray content(300 * 1024, 'q');
        server.route("GET", "/paquete", [&content](const HttpRequest&) { return HttpResponse{200, content, "application/octet-stream", {}}; });
        server.route("GET", "/roto", [](const HttpRequest&) { return HttpResponse::json(404, "{\"message\":\"Not Found\"}"); });
        GitHubReleaseSource source(QStringLiteral("dueno/qaflow"), server.baseUrl());
        QTemporaryDir dir;

        auto download = [&](const char* path, const QString& to, qint64* lastProgress = nullptr) {
            IUpdateSource::DownloadResult out;
            bool done = false;
            source.download(QUrl(server.baseUrl() + QString::fromLatin1(path)), to,
                            [lastProgress](qint64 received, qint64) { if (lastProgress) *lastProgress = received; },
                            [&](const IUpdateSource::DownloadResult& r) { out = r; done = true; });
            if (!QTest::qWaitFor([&] { return done; }, 10000)) qFatal("Sin respuesta");
            return out;
        };

        qint64 progress = 0;
        const QString ok = dir.filePath(QStringLiteral("paquete.AppImage"));
        const auto good = download("/paquete", ok, &progress);
        QVERIFY2(good.ok, qPrintable(good.error));
        QFile f(ok);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), content);
        QCOMPARE(progress, content.size());

        const QString missing = dir.filePath(QStringLiteral("roto.AppImage"));
        const auto bad = download("/roto", missing);
        QVERIFY(!bad.ok);
        QVERIFY2(bad.error.contains(QStringLiteral("404")), qPrintable(bad.error));
        QVERIFY(!QFile::exists(missing));
    }

    void preferencesRoundTrip() {
        QSettings().clear();
        QSettingsUpdatePreferences repo;
        const UpdatePreferences defaults = repo.load();
        QVERIFY(defaults.autoCheck);
        QVERIFY(defaults.channel == UpdateChannel::Stable);
        QVERIFY(!defaults.lastCheck.isValid());

        UpdatePreferences p;
        p.autoCheck = false;
        p.channel = UpdateChannel::Beta;
        p.skippedVersion = QStringLiteral("1.6.0");
        p.lastCheck = QDateTime(QDate(2026, 10, 6), QTime(9, 30), Qt::UTC);
        repo.save(p);
        const UpdatePreferences back = repo.load();
        QCOMPARE(back.autoCheck, false);
        QVERIFY(back.channel == UpdateChannel::Beta);
        QCOMPARE(back.skippedVersion, QStringLiteral("1.6.0"));
        QCOMPARE(back.lastCheck, p.lastCheck);
    }
};

QTEST_GUILESS_MAIN(UpdateSourceTest)
#include "test_update_source.moc"
