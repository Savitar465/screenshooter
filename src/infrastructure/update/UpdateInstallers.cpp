#include "UpdateInstallers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSysInfo>
#include <QUuid>

#include <filesystem>
#include <system_error>

namespace qaflow {

namespace {
QString setError(QString* error, const QString& message) {
    if (error) *error = message;
    return message;
}
} // namespace

// ---- AppImage --------------------------------------------------------------------------------

AppImageInstaller::AppImageInstaller(QString appImagePath, qint64 pid) : m_appImage(std::move(appImagePath)), m_pid(pid) {}

bool AppImageInstaller::accepts(const QString& assetName) const {
    return assetName.endsWith(QStringLiteral("-%1.AppImage").arg(QSysInfo::buildCpuArchitecture()));
}

QStringList AppImageInstaller::relaunchCommand(qint64 pid, const QString& appImage) {
    // $1 es el proceso al que se espera y $2 el AppImage: así ninguna ruta pasa por el intérprete.
    return {QStringLiteral("/bin/sh"), QStringLiteral("-c"),
            QStringLiteral("while kill -0 \"$1\" 2>/dev/null; do sleep 0.5; done; exec \"$2\""),
            QStringLiteral("qaflow-relaunch"), QString::number(pid), appImage};
}

bool AppImageInstaller::install(const QString& package, bool relaunch, QString* error) {
    const QFileInfo target(m_appImage);
    const QString dir = target.absolutePath();
    if (!QFileInfo(dir).isWritable()) {
        setError(error, QCoreApplication::translate("infrastructure", "No se puede escribir en %1: descarga la versión nueva a mano")
                            .arg(QDir::toNativeSeparators(dir)));
        return false;
    }
    // Primero una copia junto al AppImage (el mismo sistema de ficheros) y después el cambio de nombre,
    // que reemplaza de una vez: nunca queda un AppImage a medio escribir.
    const QString staged = QDir(dir).filePath(QStringLiteral(".qaflow-update-%1.AppImage").arg(QUuid::createUuid().toString(QUuid::Id128)));
    if (!QFile::copy(package, staged)) {
        setError(error, QCoreApplication::translate("infrastructure", "No se pudo copiar la versión nueva a %1").arg(QDir::toNativeSeparators(dir)));
        return false;
    }
    QFile::setPermissions(staged, target.permissions() | QFile::ExeOwner | QFile::ReadOwner | QFile::WriteOwner);
    std::error_code ec;
    std::filesystem::rename(staged.toStdString(), m_appImage.toStdString(), ec);
    if (ec) {
        QFile::remove(staged);
        setError(error, QCoreApplication::translate("infrastructure", "No se pudo reemplazar %1: %2")
                            .arg(QDir::toNativeSeparators(m_appImage), QString::fromStdString(ec.message())));
        return false;
    }
    if (!relaunch) return true;

    const QStringList command = relaunchCommand(m_pid, m_appImage);
    QProcess helper;
    helper.setProgram(command.first());
    helper.setArguments(command.mid(1));
    // Sin las variables del AppImage que se cierra: el nuevo pone las suyas al arrancar.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const char* name : {"APPIMAGE", "APPDIR", "ARGV0", "OWD", "LD_LIBRARY_PATH", "QT_PLUGIN_PATH"})
        env.remove(QString::fromLatin1(name));
    helper.setProcessEnvironment(env);
    if (!helper.startDetached()) {
        // La versión nueva ya está en su sitio: se abrirá la próxima vez, aunque ahora no se reabra sola.
        setError(error, QCoreApplication::translate("infrastructure", "La versión nueva está instalada, pero QAflow no podrá reabrirse sola"));
        return false;
    }
    return true;
}

// ---- Windows (NSIS) --------------------------------------------------------------------------

NsisInstaller::NsisInstaller(QString installDir, qint64 pid) : m_installDir(std::move(installDir)), m_pid(pid) {}

QString NsisInstaller::kind() const { return QCoreApplication::translate("infrastructure", "instalador de Windows"); }

bool NsisInstaller::accepts(const QString& assetName) const {
    return assetName.endsWith(QStringLiteral("-win64.exe"), Qt::CaseInsensitive);
}

QString NsisInstaller::script(qint64 pid, const QString& package, const QString& installDir, bool relaunch) {
    // Entre comillas simples de PowerShell sólo hay que doblar las comillas simples.
    auto quoted = [](const QString& s) { return QLatin1Char('\'') + QString(s).replace(QLatin1Char('\''), QStringLiteral("''")) + QLatin1Char('\''); };
    // Es un script de Windows se arme donde se arme (los tests corren en Linux): barras de Windows.
    auto windows = [](const QString& path) { return QString(path).replace(QLatin1Char('/'), QLatin1Char('\\')); };
    const QString dir = windows(installDir);
    // /D= va el último y sin comillas aunque la ruta tenga espacios (así lo lee NSIS): por eso los
    // argumentos van en una sola cadena, que Start-Process pasa tal cual.
    return QStringLiteral(
               "$ErrorActionPreference = 'SilentlyContinue'\n"
               "Wait-Process -Id %1\n"
               "$setup = Start-Process -FilePath %2 -ArgumentList %3 -Wait -PassThru\n"
               "if (%4 -and $setup -and $setup.ExitCode -eq 0) { Start-Process -FilePath %5 }\n")
        .arg(QString::number(pid), quoted(windows(package)), quoted(QStringLiteral("/S /D=") + dir),
             relaunch ? QStringLiteral("$true") : QStringLiteral("$false"),
             quoted(dir + QStringLiteral("\\bin\\qaflow.exe")));
}

bool NsisInstaller::install(const QString& package, bool relaunch, QString* error) {
    // El script va codificado (UTF-16LE en base64): así ninguna ruta se rompe al pasar por la línea de órdenes.
    const QString code = script(m_pid, package, m_installDir, relaunch);
    const QByteArray utf16(reinterpret_cast<const char*>(code.utf16()), code.size() * 2);
    const QStringList args{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
                           QStringLiteral("Bypass"), QStringLiteral("-WindowStyle"), QStringLiteral("Hidden"),
                           QStringLiteral("-EncodedCommand"), QString::fromLatin1(utf16.toBase64())};
    if (!QProcess::startDetached(QStringLiteral("powershell.exe"), args)) {
        setError(error, QCoreApplication::translate("infrastructure", "No se pudo preparar la instalación (PowerShell no arrancó)"));
        return false;
    }
    return true;
}

// ---- Detección -------------------------------------------------------------------------------

std::shared_ptr<IUpdateInstaller> detectUpdateInstaller() {
#if defined(Q_OS_LINUX)
    // El runtime del AppImage deja su ruta en APPIMAGE.
    const QString appImage = qEnvironmentVariable("APPIMAGE");
    if (!appImage.isEmpty() && QFileInfo(appImage).isFile()) return std::make_shared<AppImageInstaller>(appImage);
#elif defined(Q_OS_WIN)
    // El instalador deja qaflow.exe en <carpeta>\bin y su desinstalador en <carpeta>; el .zip, no.
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0 && dir.cdUp()
        && QFileInfo::exists(dir.filePath(QStringLiteral("Uninstall.exe"))))
        return std::make_shared<NsisInstaller>(dir.absolutePath());
#endif
    return nullptr;
}

} // namespace qaflow
