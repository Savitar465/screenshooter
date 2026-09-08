#pragma once

#include "core/services/ISecretStore.h"

#include <QStringList>
#include <memory>

namespace qaflow {

/// Llavero del sistema a través de su herramienta de línea de órdenes:
///  - Linux: `secret-tool` (libsecret / GNOME Keyring / KWallet vía Secret Service);
///  - macOS: `security` (Keychain).
/// El secreto viaja por stdin en Linux; en macOS la herramienta sólo lo acepta como argumento.
class CommandSecretStore : public ISecretStore {
public:
    enum class Tool { SecretTool, MacSecurity };
    explicit CommandSecretStore(Tool tool);

    std::optional<QString> read(const QString& key) override;
    bool write(const QString& key, const QString& value) override;
    void remove(const QString& key) override;
    QString description() const override;
    bool isSecure() const override { return true; }

    /// Ejecutable disponible en PATH.
    static bool available(Tool tool);

private:
    struct Run { int exitCode = -1; QString out; QString err; };
    Run run(const QStringList& args, const QByteArray& stdinData = {}) const;

    Tool m_tool;
};

#ifdef Q_OS_WIN
/// Windows: el token se cifra con DPAPI (CryptProtectData, ligado al usuario) y se guarda en QSettings.
class DpapiSecretStore : public ISecretStore {
public:
    std::optional<QString> read(const QString& key) override;
    bool write(const QString& key, const QString& value) override;
    void remove(const QString& key) override;
    QString description() const override { return QStringLiteral("Windows DPAPI (cifrado por usuario)"); }
    bool isSecure() const override { return true; }
};
#endif

/// Último recurso: QSettings en claro. La interfaz avisa de que el token queda legible.
class PlainSettingsSecretStore : public ISecretStore {
public:
    std::optional<QString> read(const QString& key) override;
    bool write(const QString& key, const QString& value) override;
    void remove(const QString& key) override;
    QString description() const override;
    bool isSecure() const override { return false; }
};

/// Elige el mejor almacén disponible en esta máquina (lo comprueba con una escritura de prueba).
std::shared_ptr<ISecretStore> makeSecretStore();

} // namespace qaflow
