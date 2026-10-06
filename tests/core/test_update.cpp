// Version y newestUpdate (core/models/Update.h): leer las etiquetas de las releases, ordenarlas con las
// reglas de SemVer y elegir la versión que se ofrece según el canal.

#include "core/models/Update.h"

#include <QtTest>

using namespace qaflow;

namespace {
Version v(const char* text) { return *Version::parse(QString::fromLatin1(text)); }

UpdateRelease release(const char* version) {
    UpdateRelease r;
    r.version = v(version);
    r.title = QString::fromLatin1(version);
    return r;
}
} // namespace

class UpdateTest : public QObject {
    Q_OBJECT
private slots:
    void parsesGitTagsAndPreReleases() {
        const auto a = Version::parse(QStringLiteral("v1.6.2"));
        QVERIFY(a);
        QCOMPARE(a->major, 1);
        QCOMPARE(a->minor, 6);
        QCOMPARE(a->patch, 2);
        QVERIFY(!a->isPreRelease());
        QCOMPARE(v("1.7.0-beta.2").preRelease, QStringLiteral("beta.2"));
        QCOMPARE(v("2.0").toString(), QStringLiteral("2.0.0"));
        QCOMPARE(v("1.5.3+build.7").toString(), QStringLiteral("1.5.3"));
    }

    void rejectsWhatIsNotAVersion() {
        QVERIFY(!Version::parse(QStringLiteral("nightly")));
        QVERIFY(!Version::parse(QStringLiteral("1")));
        QVERIFY(!Version::parse(QStringLiteral("v1.x.0")));
        QVERIFY(!Version::parse(QString()));
    }

    void ordersByNumberNotByText() {
        QVERIFY(v("1.5.3") < v("1.10.0"));
        QVERIFY(v("1.9.9") < v("2.0.0"));
        QVERIFY(v("v1.5.3") == v("1.5.3"));
    }

    void aPreReleaseGoesBeforeItsFinalVersion() {
        QVERIFY(v("1.6.0-beta") < v("1.6.0-beta.1"));
        QVERIFY(v("1.6.0-beta.2") < v("1.6.0-beta.10"));
        QVERIFY(v("1.6.0-beta.10") < v("1.6.0-rc.1"));
        QVERIFY(v("1.6.0-rc.1") < v("1.6.0"));
        QVERIFY(v("1.5.3") < v("1.6.0-beta.1"));
    }

    void theStableChannelOnlyOffersFinalVersions() {
        const QList<UpdateRelease> releases{release("1.6.0"), release("1.7.0-beta.1"), release("1.5.0")};
        const auto stable = newestUpdate(releases, v("1.5.3"), UpdateChannel::Stable);
        QVERIFY(stable);
        QCOMPARE(stable->version.toString(), QStringLiteral("1.6.0"));
        const auto beta = newestUpdate(releases, v("1.5.3"), UpdateChannel::Beta);
        QVERIFY(beta);
        QCOMPARE(beta->version.toString(), QStringLiteral("1.7.0-beta.1"));
    }

    void nothingIsOfferedWhenUpToDate() {
        const QList<UpdateRelease> releases{release("1.5.3"), release("1.5.0")};
        QVERIFY(!newestUpdate(releases, v("1.5.3"), UpdateChannel::Beta));
        QVERIFY(!newestUpdate({}, v("1.5.3"), UpdateChannel::Stable));
    }

    void readsTheChecksumsFileOfSha256sum() {
        const QByteArray text =
            "2E2A410321BE967EB9E86AF27669E98784D5323647A170D98AB4F6C04A53F784  QAflow-1.6.0-x86_64.AppImage\n"
            "1111111111111111111111111111111111111111111111111111111111111111 *qaflow-1.6.0-win64.exe\r\n"
            "esto no es una suma\n"
            "\n";
        const auto sums = parseChecksums(text);
        QCOMPARE(sums.size(), 2);
        QCOMPARE(sums.value(QStringLiteral("QAflow-1.6.0-x86_64.AppImage")),
                 QStringLiteral("2e2a410321be967eb9e86af27669e98784d5323647a170d98ab4f6c04a53f784"));
        QCOMPARE(sums.value(QStringLiteral("qaflow-1.6.0-win64.exe")), QString(64, QLatin1Char('1')));
    }

    void findsAReleaseFileByItsExactName() {
        UpdateRelease r = release("1.6.0");
        r.assets = {UpdateAsset{kChecksumsAsset, QUrl(QStringLiteral("https://example.test/s")), 10},
                    UpdateAsset{kChecksumsSignatureAsset, QUrl(QStringLiteral("https://example.test/sig")), 64}};
        QVERIFY(r.asset(kChecksumsSignatureAsset));
        QCOMPARE(r.asset(kChecksumsSignatureAsset)->size, 64);
        QVERIFY(!r.asset(QStringLiteral("sha256sums")));
    }

    void channelsRoundTrip() {
        QVERIFY(updateChannelFromString(toString(UpdateChannel::Beta)) == UpdateChannel::Beta);
        QVERIFY(updateChannelFromString(toString(UpdateChannel::Stable)) == UpdateChannel::Stable);
        QVERIFY(updateChannelFromString(QStringLiteral("cualquiera")) == UpdateChannel::Stable);
    }
};

QTEST_APPLESS_MAIN(UpdateTest)
#include "test_update.moc"
