// ed25519Verify y Ed25519Verifier (infrastructure/update/Ed25519.h): los vectores del RFC 8032 y una firma
// hecha como la hace el CI (`openssl pkeyutl -sign -rawin` con una clave de update-signing-key.sh).

#include "infrastructure/update/Ed25519.h"

#include <QtTest>

using namespace qaflow;

namespace {
QByteArray hex(const char* s) { return QByteArray::fromHex(s); }

// RFC 8032, 7.1, TEST 1 (mensaje vacío) y TEST 2 (un byte).
const char* kRfcKey1 = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
const char* kRfcSig1 = "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b";
const char* kRfcKey2 = "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c";
const char* kRfcSig2 = "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00";

// Clave de prueba (sólo para este test) y unas sumas firmadas con ella por OpenSSL.
const QByteArray kCiKey = "IiW8G0ljBIUJvz3MeL4iV4MAJ+0QAhZsf5c6QLI0wQk=";
const QByteArray kCiSums = QByteArray::fromBase64(
    "MmUyYTQxMDMyMWJlOTY3ZWI5ZTg2YWYyNzY2OWU5ODc4NGQ1MzIzNjQ3YTE3MGQ5OGFiNGY2YzA0YTUzZjc4NCAgUUFmbG93LTEuNi4wLXg4Nl82NC5BcHBJbWFnZQo=");
const QByteArray kCiSignature = QByteArray::fromBase64(
    "ejDP/Qp1Cl8BDnUtWG/ZIqiZOVFmnf3zTwgIl+Xfn0/Aclgh6g4uLJMR/AoXrF+ENuB9u+h8ugDmJskb+1z7Bw==");
} // namespace

class Ed25519Test : public QObject {
    Q_OBJECT
private slots:
    void acceptsTheRfcVectors() {
        QVERIFY(ed25519Verify(QByteArray(), hex(kRfcSig1), hex(kRfcKey1)));
        QVERIFY(ed25519Verify(hex("72"), hex(kRfcSig2), hex(kRfcKey2)));
    }

    void rejectsAnyChange() {
        QVERIFY(!ed25519Verify(hex("73"), hex(kRfcSig2), hex(kRfcKey2)));       // otro mensaje
        QVERIFY(!ed25519Verify(hex("72"), hex(kRfcSig2), hex(kRfcKey1)));       // otra clave
        QByteArray sig = hex(kRfcSig2);
        sig[10] = char(sig[10] ^ 0x01);
        QVERIFY(!ed25519Verify(hex("72"), sig, hex(kRfcKey2)));                 // firma tocada
        QVERIFY(!ed25519Verify(hex("72"), hex(kRfcSig2).left(63), hex(kRfcKey2)));
        QVERIFY(!ed25519Verify(hex("72"), hex(kRfcSig2), hex(kRfcKey2).left(31)));
    }

    void verifiesWhatTheCiSigns() {
        Ed25519Verifier verifier(kCiKey);
        QVERIFY(verifier.isValid());
        QVERIFY(verifier.verify(kCiSums, kCiSignature));
        QVERIFY(verifier.verify(kCiSums, kCiSignature.toBase64()));   // también en base64
        QByteArray tampered = kCiSums;
        tampered[0] = tampered[0] == '0' ? '1' : '0';
        QVERIFY(!verifier.verify(tampered, kCiSignature));
    }

    void aMissingKeyVerifiesNothing() {
        Ed25519Verifier verifier("");
        QVERIFY(!verifier.isValid());
        QVERIFY(!verifier.verify(kCiSums, kCiSignature));
    }
};

QTEST_APPLESS_MAIN(Ed25519Test)
#include "test_ed25519.moc"
