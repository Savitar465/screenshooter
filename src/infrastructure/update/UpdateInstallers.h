#pragma once

#include "core/services/IUpdateInstaller.h"

#include <QCoreApplication>
#include <QStringList>
#include <memory>

namespace qaflow {

/// QAflow como AppImage: el paquete nuevo reemplaza al fichero en el sitio (con un `rename`, así que de
/// golpe). Lo que ya está abierto sigue funcionando: Linux mantiene vivo el fichero viejo hasta que se
/// cierra. Con `relaunch`, un proceso aparte espera a que QAflow termine y abre el nuevo.
class AppImageInstaller : public IUpdateInstaller {
public:
    explicit AppImageInstaller(QString appImagePath, qint64 pid = QCoreApplication::applicationPid());

    QString kind() const override { return QStringLiteral("AppImage"); }
    /// "QAflow-1.6.0-x86_64.AppImage", con la arquitectura de esta compilación.
    bool accepts(const QString& assetName) const override;
    bool install(const QString& package, bool relaunch, QString* error) override;

    /// Programa y argumentos del proceso que espera a que termine `pid` y abre `appImage`.
    static QStringList relaunchCommand(qint64 pid, const QString& appImage);

private:
    QString m_appImage;
    qint64 m_pid;
};

/// QAflow instalado con el instalador NSIS: cuando QAflow se cierra, un proceso aparte ejecuta el
/// instalador nuevo en silencio (`/S`, que desinstala antes la versión anterior) en la misma carpeta. Pide
/// permisos de administrador, como al instalar a mano; si no se conceden, se queda la versión que había.
class NsisInstaller : public IUpdateInstaller {
public:
    /// `installDir` es la carpeta de la instalación (la de `Uninstall.exe`, encima de `bin`).
    explicit NsisInstaller(QString installDir, qint64 pid = QCoreApplication::applicationPid());

    QString kind() const override;
    /// "qaflow-1.6.0-win64.exe".
    bool accepts(const QString& assetName) const override;
    bool install(const QString& package, bool relaunch, QString* error) override;

    /// El script de PowerShell que espera a `pid`, instala `package` en `installDir` y, con `relaunch`,
    /// vuelve a abrir QAflow si la instalación terminó bien.
    static QString script(qint64 pid, const QString& package, const QString& installDir, bool relaunch);

private:
    QString m_installDir;
    qint64 m_pid;
};

/// El instalador que corresponde a esta instalación de QAflow; nullptr si la actualiza otro (el paquete de
/// la distribución, un .zip o .tar.gz portátil, macOS) o es una compilación de desarrollo.
std::shared_ptr<IUpdateInstaller> detectUpdateInstaller();

} // namespace qaflow
