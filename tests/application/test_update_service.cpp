// UpdateService (application/UpdateService.h): cuándo busca solo, de qué avisa y qué recuerda, con una
// fuente de versiones falsa que responde cuando el test lo dice.

#include "application/UpdateService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace qaflow;

namespace {
Version v(const char* text) { return *Version::parse(QString::fromLatin1(text)); }

UpdateRelease release(const char* version) {
    UpdateRelease r;
    r.version = v(version);
    r.title = QString::fromLatin1(version);
    r.pageUrl = QUrl(QStringLiteral("https://example.test/releases/") + QString::fromLatin1(version));
    return r;
}

/// Guarda las peticiones y las responde cuando se llama a `reply`; las descargas, con `finishDownloads`,
/// escribiendo lo que haya en `files` para cada URL (sin entrada, la descarga falla).
class FakeUpdateSource : public IUpdateSource {
public:
    void fetchReleases(std::function<void(const UpdateCheckResult&)> done) override { pending << std::move(done); }
    void reply(const UpdateCheckResult& r) {
        const auto waiting = std::exchange(pending, {});
        for (const auto& done : waiting) done(r);
    }
    void replyWith(const QList<UpdateRelease>& releases) { reply(UpdateCheckResult{true, {}, releases}); }

    struct Download {
        QUrl url;
        QString path;
        std::function<void(qint64, qint64)> progress;
        std::function<void(const DownloadResult&)> done;
    };
    void download(const QUrl& url, const QString& path, std::function<void(qint64, qint64)> progress,
                  std::function<void(const DownloadResult&)> done) override {
        downloads << Download{url, path, std::move(progress), std::move(done)};
    }
    void cancelDownloads() override {
        const auto cut = std::exchange(downloads, {});
        for (const auto& d : cut) d.done(DownloadResult{false, QStringLiteral("Cancelada")});
    }
    /// Completa las descargas pendientes y las que éstas encadenen.
    void finishDownloads() {
        while (!downloads.isEmpty()) {
            const Download d = downloads.takeFirst();
            if (!files.contains(d.url)) { d.done(DownloadResult{false, QStringLiteral("HTTP 404")}); continue; }
            QFile f(d.path);
            f.open(QIODevice::WriteOnly);
            f.write(files[d.url]);
            f.close();
            if (d.progress) d.progress(files[d.url].size(), files[d.url].size());
            d.done(DownloadResult{true, {}});
        }
    }

    QList<std::function<void(const UpdateCheckResult&)>> pending;
    QList<Download> downloads;
    QHash<QUrl, QByteArray> files;
};

class FakeInstaller : public IUpdateInstaller {
public:
    QString kind() const override { return QStringLiteral("AppImage"); }
    bool accepts(const QString& name) const override { return name.endsWith(QStringLiteral(".AppImage")); }
    bool install(const QString& package, bool relaunch, QString* error) override {
        installed = package;
        relaunched = relaunch;
        if (fail && error) *error = QStringLiteral("Sin permiso");
        return !fail;
    }
    QString installed;
    bool relaunched = false;
    bool fail = false;
};

/// "Firma" de prueba: vale si es "firma:" seguido del contenido.
class FakeVerifier : public ISignatureVerifier {
public:
    bool verify(const QByteArray& data, const QByteArray& signature) const override { return signature == "firma:" + data; }
};

class MemoryUpdatePreferences : public IUpdatePreferencesRepository {
public:
    UpdatePreferences load() override { return stored; }
    void save(const UpdatePreferences& p) override { stored = p; ++saves; }

    UpdatePreferences stored;
    int saves = 0;
};
} // namespace

class UpdateServiceTest : public QObject {
    Q_OBJECT

    std::shared_ptr<FakeUpdateSource> m_source;
    std::shared_ptr<MemoryUpdatePreferences> m_prefs;

    std::unique_ptr<UpdateService> service(const char* current = "1.5.3") {
        return std::make_unique<UpdateService>(m_source, m_prefs, v(current));
    }

private slots:
    void init() {
        m_source = std::make_shared<FakeUpdateSource>();
        m_prefs = std::make_shared<MemoryUpdatePreferences>();
    }

    /// Una release 1.6.0 con su AppImage, las sumas y su firma, ya publicadas en la fuente falsa.
    UpdateRelease installableRelease(const QByteArray& package = "binario nuevo", bool goodSignature = true,
                                     const QByteArray& sumsOverride = {}) {
        UpdateRelease r = release("1.6.0");
        const QUrl pkg(QStringLiteral("https://example.test/QAflow-1.6.0-x86_64.AppImage"));
        const QUrl sums(QStringLiteral("https://example.test/SHA256SUMS"));
        const QUrl sig(QStringLiteral("https://example.test/SHA256SUMS.sig"));
        r.assets = {UpdateAsset{QStringLiteral("qaflow_1.6.0_amd64.deb"), QUrl(QStringLiteral("https://example.test/deb")), 1},
                    UpdateAsset{QStringLiteral("QAflow-1.6.0-x86_64.AppImage"), pkg, package.size()},
                    UpdateAsset{kChecksumsAsset, sums, 0}, UpdateAsset{kChecksumsSignatureAsset, sig, 0}};
        const QByteArray text = sumsOverride.isEmpty()
            ? QCryptographicHash::hash(package, QCryptographicHash::Sha256).toHex() + "  QAflow-1.6.0-x86_64.AppImage\n"
            : sumsOverride;
        m_source->files[pkg] = package;
        m_source->files[sums] = text;
        m_source->files[sig] = goodSignature ? "firma:" + text : QByteArray("firma falsa");
        return r;
    }

    /// Un servicio con la 1.6.0 instalable ya encontrada y un instalador falso.
    std::unique_ptr<UpdateService> foundInstallable(const QTemporaryDir& dir, const std::shared_ptr<FakeInstaller>& installer,
                                                    const UpdateRelease& r) {
        auto s = service();
        s->setInstaller(installer, std::make_shared<FakeVerifier>(), dir.path());
        s->checkNow();
        m_source->replyWith({r});
        return s;
    }

    void anAutomaticCheckAnnouncesANewVersionOnce() {
        auto s = service();
        QSignalSpy announced(s.get(), &UpdateService::updateAvailable);
        s->checkIfDue();
        QCOMPARE(s->state(), UpdateService::State::Checking);
        m_source->replyWith({release("1.6.0"), release("1.5.3")});
        QCOMPARE(s->state(), UpdateService::State::Available);
        QCOMPARE(s->available()->version.toString(), QStringLiteral("1.6.0"));
        QCOMPARE(announced.count(), 1);
        QVERIFY(m_prefs->stored.lastCheck.isValid());

        // Otra búsqueda automática (cambiar de canal la lanza) con la misma versión: ya se avisó.
        s->setChannel(UpdateChannel::Beta);
        QCOMPARE(m_source->pending.size(), 1);
        m_source->replyWith({release("1.6.0")});
        QCOMPARE(announced.count(), 1);
    }

    void isDueOncePerDayAndOnlyWithAutoCheck() {
        auto s = service();
        const QDateTime now = QDateTime::currentDateTimeUtc();
        QVERIFY(s->isDue(now));   // nunca se buscó
        m_prefs->stored.lastCheck = now.addSecs(-3600);
        s = service();
        QVERIFY(!s->isDue(now));
        QVERIFY(s->isDue(now.addSecs(UpdateService::kCheckIntervalSecs)));
        s->setAutoCheck(false);
        QVERIFY(!s->isDue(now.addSecs(UpdateService::kCheckIntervalSecs)));
        QVERIFY(!m_prefs->stored.autoCheck);
    }

    void aSkippedVersionIsNotAnnouncedButAManualCheckShowsIt() {
        m_prefs->stored.skippedVersion = QStringLiteral("1.6.0");
        auto s = service();
        QSignalSpy announced(s.get(), &UpdateService::updateAvailable);
        s->checkIfDue();
        m_source->replyWith({release("1.6.0")});
        QCOMPARE(s->state(), UpdateService::State::UpToDate);
        QCOMPARE(announced.count(), 0);

        bool done = false;
        s->checkNow([&done]() { done = true; });
        m_source->replyWith({release("1.6.0")});
        QVERIFY(done);
        QCOMPARE(s->state(), UpdateService::State::Available);
        QCOMPARE(announced.count(), 0);   // la manual la enseña quien la pidió
    }

    void skippingAVersionForgetsItButNotTheNextOne() {
        auto s = service();
        s->checkNow();
        m_source->replyWith({release("1.6.0")});
        s->skip(v("1.6.0"));
        QVERIFY(!s->available());
        QCOMPARE(m_prefs->stored.skippedVersion, QStringLiteral("1.6.0"));

        QSignalSpy announced(s.get(), &UpdateService::updateAvailable);
        s->setChannel(UpdateChannel::Beta);   // lanza una búsqueda automática
        m_source->replyWith({release("1.6.0")});
        QCOMPARE(announced.count(), 0);
        s->setChannel(UpdateChannel::Stable);
        m_source->replyWith({release("1.6.1"), release("1.6.0")});
        QCOMPARE(announced.count(), 1);
        QCOMPARE(s->available()->version.toString(), QStringLiteral("1.6.1"));
    }

    void aFailedCheckKeepsWhatWasFoundAndReportsTheError() {
        auto s = service();
        s->checkNow();
        m_source->replyWith({release("1.6.0")});
        const QDateTime lastCheck = m_prefs->stored.lastCheck;
        s->checkNow();
        m_source->reply(UpdateCheckResult{false, QStringLiteral("Sin red"), {}});
        QCOMPARE(s->state(), UpdateService::State::Failed);
        QCOMPARE(s->lastError(), QStringLiteral("Sin red"));
        QVERIFY(s->available());
        QCOMPARE(m_prefs->stored.lastCheck, lastCheck);   // no cuenta como búsqueda hecha
    }

    void overlappingChecksShareOneRequest() {
        auto s = service();
        int calls = 0;
        s->checkIfDue();
        s->checkNow([&calls]() { ++calls; });
        s->checkNow([&calls]() { ++calls; });
        QCOMPARE(m_source->pending.size(), 1);
        QSignalSpy announced(s.get(), &UpdateService::updateAvailable);
        m_source->replyWith({release("1.6.0")});
        QCOMPARE(calls, 2);
        QCOMPARE(announced.count(), 0);   // la contestó una manual: la ve quien la pidió
    }

    void withoutInstallerOrSignedSumsItOnlyOffersTheDownload() {
        QTemporaryDir dir;
        auto s = service();
        s->checkNow();
        m_source->replyWith({installableRelease()});
        QVERIFY(s->available());
        QVERIFY(!s->canInstall());   // sin instalador

        auto installer = std::make_shared<FakeInstaller>();
        UpdateRelease unsigned_ = installableRelease();
        unsigned_.assets.removeIf([](const UpdateAsset& a) { return a.name == kChecksumsSignatureAsset; });
        s = foundInstallable(dir, installer, unsigned_);
        QVERIFY(!s->canInstall());   // sin firma

        s = foundInstallable(dir, installer, installableRelease());
        QVERIFY(s->canInstall());
        QCOMPARE(s->installKind(), QStringLiteral("AppImage"));
    }

    void downloadsVerifiesAndInstallsThePackage() {
        QTemporaryDir dir;
        auto installer = std::make_shared<FakeInstaller>();
        auto s = foundInstallable(dir, installer, installableRelease("binario nuevo"));
        QSignalSpy progress(s.get(), &UpdateService::downloadProgress);
        s->downloadUpdate();
        QVERIFY(s->installState() == UpdateService::InstallState::Downloading);
        m_source->finishDownloads();
        QVERIFY2(s->installState() == UpdateService::InstallState::Ready, qPrintable(s->installError()));
        QVERIFY(progress.count() > 0);

        QVERIFY(s->install(true));
        QVERIFY(s->installState() == UpdateService::InstallState::Scheduled);
        QVERIFY(installer->relaunched);
        QFile f(installer->installed);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("binario nuevo"));
        QVERIFY(installer->installed.startsWith(dir.path()));
    }

    void aBadSignatureStopsBeforeDownloadingThePackage() {
        QTemporaryDir dir;
        auto installer = std::make_shared<FakeInstaller>();
        auto s = foundInstallable(dir, installer, installableRelease("binario nuevo", false));
        s->downloadUpdate();
        m_source->finishDownloads();
        QVERIFY(s->installState() == UpdateService::InstallState::Failed);
        QVERIFY(s->installError().contains(QStringLiteral("firma")));
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QStringLiteral("1.6.0/QAflow-1.6.0-x86_64.AppImage"))));
        QVERIFY(!s->install(false));
        QVERIFY(installer->installed.isEmpty());
    }

    void aPackageThatDoesNotMatchItsSumIsDiscarded() {
        QTemporaryDir dir;
        auto installer = std::make_shared<FakeInstaller>();
        const QByteArray otherSum = QCryptographicHash::hash("otro binario", QCryptographicHash::Sha256).toHex()
                                    + "  QAflow-1.6.0-x86_64.AppImage\n";
        auto s = foundInstallable(dir, installer, installableRelease("binario nuevo", true, otherSum));
        s->downloadUpdate();
        m_source->finishDownloads();
        QVERIFY(s->installState() == UpdateService::InstallState::Failed);
        QVERIFY(!QFile::exists(QDir(dir.path()).filePath(QStringLiteral("1.6.0/QAflow-1.6.0-x86_64.AppImage"))));
    }

    void cancellingLeavesNothingPending() {
        QTemporaryDir dir;
        auto installer = std::make_shared<FakeInstaller>();
        auto s = foundInstallable(dir, installer, installableRelease());
        s->downloadUpdate();
        s->cancelDownload();
        QVERIFY(s->installState() == UpdateService::InstallState::Idle);
        QVERIFY(s->installError().isEmpty());
        m_source->finishDownloads();   // nada que completar: las respuestas cortadas no cuentan
        QVERIFY(s->installState() == UpdateService::InstallState::Idle);
    }

    void aFailedInstallIsReported() {
        QTemporaryDir dir;
        auto installer = std::make_shared<FakeInstaller>();
        installer->fail = true;
        auto s = foundInstallable(dir, installer, installableRelease());
        s->downloadUpdate();
        m_source->finishDownloads();
        QVERIFY(!s->install(false));
        QVERIFY(s->installState() == UpdateService::InstallState::Failed);
        QCOMPARE(s->installError(), QStringLiteral("Sin permiso"));
    }

    void oldDownloadsAreCleanedOnStart() {
        QTemporaryDir dir;
        for (const char* name : {"1.5.0", "1.5.3", "1.6.0", "otra-cosa"}) QVERIFY(QDir(dir.path()).mkpath(QString::fromLatin1(name)));
        auto s = service("1.5.3");
        s->setInstaller(std::make_shared<FakeInstaller>(), std::make_shared<FakeVerifier>(), dir.path());
        const QStringList left = QDir(dir.path()).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        QCOMPARE(left, (QStringList{QStringLiteral("1.6.0"), QStringLiteral("otra-cosa")}));
    }

    void theBetaChannelOffersPreReleasesAndLeavingItDropsThem() {
        auto s = service();
        s->setChannel(UpdateChannel::Beta);   // con búsqueda automática, cambiar de canal vuelve a buscar
        QCOMPARE(m_source->pending.size(), 1);
        m_source->replyWith({release("1.6.0-beta.1"), release("1.5.3")});
        QCOMPARE(s->available()->version.toString(), QStringLiteral("1.6.0-beta.1"));
        s->setChannel(UpdateChannel::Stable);
        QVERIFY(!s->available());
        QVERIFY(m_prefs->stored.channel == UpdateChannel::Stable);
    }
};

QTEST_GUILESS_MAIN(UpdateServiceTest)
#include "test_update_service.moc"
