#include "UpdateService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>

namespace qaflow {

namespace {
constexpr int kStartupDelayMs = 15 * 1000;   // que la búsqueda no compita con abrir el proyecto
constexpr int kPollMs = 60 * 60 * 1000;

QByteArray readFile(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

QString sha256Of(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&f)) return {};
    return QString::fromLatin1(hash.result().toHex());
}
} // namespace

UpdateService::UpdateService(std::shared_ptr<IUpdateSource> source, std::shared_ptr<IUpdatePreferencesRepository> prefs,
                             Version current, QObject* parent)
    : QObject(parent), m_source(std::move(source)), m_repo(std::move(prefs)), m_current(std::move(current)) {
    if (m_repo) m_prefs = m_repo->load();
    m_poll.setInterval(kPollMs);
    connect(&m_poll, &QTimer::timeout, this, &UpdateService::checkIfDue);
}

void UpdateService::setAutoCheck(bool on) {
    if (m_prefs.autoCheck == on) return;
    m_prefs.autoCheck = on;
    if (m_repo) m_repo->save(m_prefs);
    emit changed();
}

void UpdateService::setChannel(UpdateChannel channel) {
    if (m_prefs.channel == channel) return;
    m_prefs.channel = channel;
    if (m_repo) m_repo->save(m_prefs);
    // Una beta encontrada deja de valer en el canal estable.
    if (m_available && m_available->version.isPreRelease() && channel == UpdateChannel::Stable) {
        m_available.reset();
        m_state = State::UpToDate;
    }
    emit changed();
    if (m_prefs.autoCheck) check(false, {});
}

void UpdateService::start() {
    QTimer::singleShot(kStartupDelayMs, this, &UpdateService::checkIfDue);
    m_poll.start();
}

bool UpdateService::isDue(const QDateTime& now) const {
    if (!m_prefs.autoCheck) return false;
    return !m_prefs.lastCheck.isValid() || m_prefs.lastCheck.secsTo(now) >= kCheckIntervalSecs
           || m_prefs.lastCheck > now;   // reloj atrasado: mejor buscar que quedarse sin avisos
}

void UpdateService::checkIfDue() {
    if (isDue(QDateTime::currentDateTimeUtc())) check(false, {});
}

void UpdateService::checkNow(std::function<void()> done) { check(true, std::move(done)); }

void UpdateService::check(bool manual, std::function<void()> done) {
    if (done) m_waiting << std::move(done);
    m_manual = m_manual || manual;
    if (m_state == State::Checking) return;   // la que está en marcha responde por las dos
    if (!m_source) {
        finish(UpdateCheckResult{false, tr("No hay de dónde buscar actualizaciones"), {}});
        return;
    }
    m_state = State::Checking;
    emit changed();
    m_source->fetchReleases([this](const UpdateCheckResult& r) { finish(r); });
}

void UpdateService::finish(const UpdateCheckResult& result) {
    const bool manual = m_manual;
    m_manual = false;
    if (!result.ok) {
        // La versión encontrada antes sigue siendo válida aunque esta búsqueda no haya respondido.
        m_state = State::Failed;
        m_error = result.error;
    } else {
        m_error.clear();
        m_prefs.lastCheck = QDateTime::currentDateTimeUtc();
        if (m_repo) m_repo->save(m_prefs);
        m_available = newestUpdate(result.releases, m_current, m_prefs.channel);
        if (m_available && !manual && m_available->version.toString() == m_prefs.skippedVersion) m_available.reset();
        m_state = m_available ? State::Available : State::UpToDate;
    }
    // Lo descargado para otra versión (o para ninguna) ya no vale; una instalación en curso o hecha, sí.
    if ((m_installState == InstallState::Ready || m_installState == InstallState::Failed)
        && (!m_available || !(m_available->version == m_installVersion)))
        setInstallState(InstallState::Idle);
    // Se avisa una vez por versión: la búsqueda manual ya la enseña ella misma.
    const bool announce = m_available && m_available->version.toString() != m_announced;
    if (m_available) m_announced = m_available->version.toString();
    emit changed();
    if (announce && !manual) emit updateAvailable(*m_available);
    const auto waiting = std::exchange(m_waiting, {});
    for (const auto& done : waiting) done();
}

void UpdateService::skip(const Version& version) {
    m_prefs.skippedVersion = version.toString();
    if (m_repo) m_repo->save(m_prefs);
    if (m_available && m_available->version == version) {
        m_available.reset();
        m_state = State::UpToDate;
    }
    emit changed();
}

void UpdateService::setInstaller(std::shared_ptr<IUpdateInstaller> installer, std::shared_ptr<ISignatureVerifier> verifier,
                                 QString downloadDir) {
    m_installer = std::move(installer);
    m_verifier = std::move(verifier);
    m_downloadDir = std::move(downloadDir);
    // Los paquetes de la versión que ya corre (o de anteriores) ya se instalaron: fuera.
    if (!m_downloadDir.isEmpty()) {
        const QDir dir(m_downloadDir);
        for (const QString& name : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            if (const auto v = Version::parse(name); v && !(m_current < *v)) QDir(dir.filePath(name)).removeRecursively();
    }
    emit changed();
}

const UpdateAsset* UpdateService::packageAsset(const UpdateRelease& release) const {
    if (!m_installer) return nullptr;
    for (const UpdateAsset& a : release.assets)
        if (m_installer->accepts(a.name)) return &a;
    return nullptr;
}

bool UpdateService::canInstall() const {
    return m_available && m_installer && m_verifier && m_source && !m_downloadDir.isEmpty() && packageAsset(*m_available)
           && m_available->asset(kChecksumsAsset) && m_available->asset(kChecksumsSignatureAsset);
}

QString UpdateService::installKind() const { return m_installer ? m_installer->kind() : QString(); }

void UpdateService::setInstallState(InstallState state, const QString& error) {
    m_installState = state;
    m_installError = error;
    emit installChanged();
}

void UpdateService::downloadUpdate() {
    if (!canInstall() || m_installState == InstallState::Downloading || m_installState == InstallState::Scheduled) return;
    const UpdateRelease release = *m_available;
    const UpdateAsset package = *packageAsset(release);
    const UpdateAsset sums = *release.asset(kChecksumsAsset);
    const UpdateAsset signature = *release.asset(kChecksumsSignatureAsset);
    QDir dir(QDir(m_downloadDir).filePath(release.version.toString()));
    dir.removeRecursively();   // lo de un intento anterior no se reutiliza: se vuelve a comprobar todo
    if (!QDir().mkpath(dir.path())) {
        setInstallState(InstallState::Failed, tr("No se pudo crear la carpeta de descargas %1").arg(QDir::toNativeSeparators(dir.path())));
        return;
    }
    m_installVersion = release.version;
    m_package.clear();
    const int id = ++m_downloadId;
    setInstallState(InstallState::Downloading);

    const QString sumsPath = dir.filePath(sums.name);
    const QString signaturePath = dir.filePath(signature.name);
    const QString packagePath = dir.filePath(package.name);
    auto current = [this, id]() { return id == m_downloadId; };
    auto fail = [this](const QString& error) { setInstallState(InstallState::Failed, error); };

    // 1 · las sumas y su firma; 2 · que la firma sea de quien publica QAflow; 3 · el paquete y su suma.
    m_source->download(sums.url, sumsPath, {}, [=, this](const IUpdateSource::DownloadResult& r) {
        if (!current()) return;
        if (!r.ok) { fail(r.error); return; }
        m_source->download(signature.url, signaturePath, {}, [=, this](const IUpdateSource::DownloadResult& r) {
            if (!current()) return;
            if (!r.ok) { fail(r.error); return; }
            const QByteArray sumsText = readFile(sumsPath);
            if (!m_verifier->verify(sumsText, readFile(signaturePath))) {
                fail(tr("La firma de la versión %1 no es válida: no se instala. Descárgala desde su página.")
                         .arg(release.version.toString()));
                return;
            }
            const QString expected = parseChecksums(sumsText).value(package.name);
            if (expected.isEmpty()) {
                fail(tr("Las sumas de comprobación no incluyen %1").arg(package.name));
                return;
            }
            m_source->download(package.url, packagePath,
                               [this, current](qint64 received, qint64 total) { if (current()) emit downloadProgress(received, total); },
                               [=, this](const IUpdateSource::DownloadResult& r) {
                if (!current()) return;
                if (!r.ok) { fail(r.error); return; }
                if (sha256Of(packagePath) != expected) {
                    QFile::remove(packagePath);
                    fail(tr("El paquete descargado no coincide con su suma de comprobación: no se instala"));
                    return;
                }
                m_package = packagePath;
                setInstallState(InstallState::Ready);
            });
        });
    });
}

void UpdateService::cancelDownload() {
    if (m_installState != InstallState::Downloading) return;
    ++m_downloadId;
    if (m_source) m_source->cancelDownloads();
    setInstallState(InstallState::Idle);
}

bool UpdateService::install(bool relaunch) {
    if (m_installState != InstallState::Ready || !m_installer || m_package.isEmpty()) return false;
    QString error;
    if (!m_installer->install(m_package, relaunch, &error)) {
        setInstallState(InstallState::Failed, error.isEmpty() ? tr("No se pudo instalar la versión nueva") : error);
        return false;
    }
    setInstallState(InstallState::Scheduled);
    return true;
}

} // namespace qaflow
