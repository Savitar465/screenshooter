#pragma once

#include <QByteArray>
#include <QString>

namespace qaflow {

/// Cómo se instala una versión nueva según cómo se instaló QAflow (AppImage, instalador de Windows).
/// Las instalaciones que gestiona otro (un .deb, un .zip portátil, macOS) no tienen instalador: en
/// ellas sólo se ofrece la descarga.
class IUpdateInstaller {
public:
    virtual ~IUpdateInstaller() = default;
    /// Para la interfaz: "AppImage", "instalador de Windows".
    virtual QString kind() const = 0;
    /// ¿Es éste el paquete de la release que sabe instalar? ("QAflow-1.6.0-x86_64.AppImage")
    virtual bool accepts(const QString& assetName) const = 0;
    /// Deja instalado (o programado para cuando QAflow se cierre) el paquete ya verificado. Con
    /// `relaunch`, QAflow vuelve a abrirse en cuanto se cierre esta instancia. Nunca cierra la aplicación:
    /// eso lo decide quien llama.
    virtual bool install(const QString& package, bool relaunch, QString* error) = 0;
};

/// Comprueba la firma de un contenido con la clave pública de quien publica QAflow.
class ISignatureVerifier {
public:
    virtual ~ISignatureVerifier() = default;
    virtual bool verify(const QByteArray& data, const QByteArray& signature) const = 0;
};

} // namespace qaflow
