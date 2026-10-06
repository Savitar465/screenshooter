#pragma once

#include "core/services/IUpdateInstaller.h"

#include <QByteArray>

namespace qaflow {

/// Verificación de firmas Ed25519 (RFC 8032): una firma de 64 bytes de `message` con la clave pública de
/// 32 bytes. Sólo verifica (QAflow nunca firma) y no depende de OpenSSL, que Qt no expone.
bool ed25519Verify(const QByteArray& message, const QByteArray& signature, const QByteArray& publicKey);

/// Verificador de las sumas de una release con la clave pública de quien publica QAflow, en base64 (la que
/// imprime `packaging/release/update-signing-key.sh`). La firma puede venir en binario (64 bytes) o en
/// base64.
class Ed25519Verifier : public ISignatureVerifier {
public:
    explicit Ed25519Verifier(const QByteArray& publicKeyBase64);
    /// ¿La clave tiene la forma de una clave Ed25519 (32 bytes)?
    bool isValid() const { return m_publicKey.size() == 32; }
    bool verify(const QByteArray& data, const QByteArray& signature) const override;

private:
    QByteArray m_publicKey;
};

} // namespace qaflow
