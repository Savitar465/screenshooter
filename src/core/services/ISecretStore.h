#pragma once

#include <QString>
#include <optional>

namespace qaflow {

/// Almacén de secretos (tokens de API). La implementación decide dónde: llavero del sistema,
/// DPAPI… o, como último recurso, un fichero en claro que la interfaz debe señalar.
class ISecretStore {
public:
    virtual ~ISecretStore() = default;

    virtual std::optional<QString> read(const QString& key) = 0;
    virtual bool write(const QString& key, const QString& value) = 0;
    virtual void remove(const QString& key) = 0;

    /// Descripción para el usuario ("Llavero del sistema (secret-tool)", "Sin cifrar").
    virtual QString description() const = 0;
    /// false si el secreto queda legible en disco.
    virtual bool isSecure() const = 0;
};

} // namespace qaflow
