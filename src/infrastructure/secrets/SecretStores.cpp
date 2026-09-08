#include "SecretStores.h"

#include <QCoreApplication>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

namespace qaflow {

namespace {
const QString kService = QStringLiteral("qaflow");
constexpr int kToolTimeoutMs = 5000;
} // namespace

// ---- CommandSecretStore ---------------------------------------------------------------------

CommandSecretStore::CommandSecretStore(Tool tool) : m_tool(tool) {}

bool CommandSecretStore::available(Tool tool) {
    switch (tool) {
        case Tool::SecretTool: return !QStandardPaths::findExecutable(QStringLiteral("secret-tool")).isEmpty();
        case Tool::MacSecurity: return !QStandardPaths::findExecutable(QStringLiteral("security")).isEmpty();
    }
    return false;
}

CommandSecretStore::Run CommandSecretStore::run(const QStringList& args, const QByteArray& stdinData) const {
    const QString exe = m_tool == Tool::SecretTool ? QStringLiteral("secret-tool") : QStringLiteral("security");
    QProcess p;
    p.start(exe, args);
    Run r;
    if (!p.waitForStarted(kToolTimeoutMs)) return r;
    if (!stdinData.isEmpty()) p.write(stdinData);
    p.closeWriteChannel();
    if (!p.waitForFinished(kToolTimeoutMs)) { p.kill(); return r; }
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    r.out = QString::fromUtf8(p.readAllStandardOutput());
    r.err = QString::fromUtf8(p.readAllStandardError());
    return r;
}

std::optional<QString> CommandSecretStore::read(const QString& key) {
    Run r;
    if (m_tool == Tool::SecretTool) r = run({QStringLiteral("lookup"), QStringLiteral("application"), kService, QStringLiteral("key"), key});
    else r = run({QStringLiteral("find-generic-password"), QStringLiteral("-a"), kService, QStringLiteral("-s"), key, QStringLiteral("-w")});
    if (r.exitCode != 0) return std::nullopt;
    QString value = r.out;
    while (value.endsWith(QLatin1Char('\n')) || value.endsWith(QLatin1Char('\r'))) value.chop(1);
    return value;
}

bool CommandSecretStore::write(const QString& key, const QString& value) {
    if (m_tool == Tool::SecretTool)
        return run({QStringLiteral("store"), QStringLiteral("--label=QAflow ") + key, QStringLiteral("application"), kService, QStringLiteral("key"), key}, value.toUtf8()).exitCode == 0;
    return run({QStringLiteral("add-generic-password"), QStringLiteral("-a"), kService, QStringLiteral("-s"), key, QStringLiteral("-w"), value, QStringLiteral("-U")}).exitCode == 0;
}

void CommandSecretStore::remove(const QString& key) {
    if (m_tool == Tool::SecretTool) run({QStringLiteral("clear"), QStringLiteral("application"), kService, QStringLiteral("key"), key});
    else run({QStringLiteral("delete-generic-password"), QStringLiteral("-a"), kService, QStringLiteral("-s"), key});
}

QString CommandSecretStore::description() const {
    return m_tool == Tool::SecretTool ? QCoreApplication::translate("infrastructure", "Llavero del sistema (secret-tool)") : QCoreApplication::translate("infrastructure", "Llavero de macOS (Keychain)");
}

// ---- DpapiSecretStore -----------------------------------------------------------------------

#ifdef Q_OS_WIN
namespace {
const QString kGroup = QStringLiteral("secrets");
}

std::optional<QString> DpapiSecretStore::read(const QString& key) {
    QSettings s;
    const QByteArray blob = QByteArray::fromBase64(s.value(kGroup + QLatin1Char('/') + key).toByteArray());
    if (blob.isEmpty()) return std::nullopt;
    DATA_BLOB in{static_cast<DWORD>(blob.size()), reinterpret_cast<BYTE*>(const_cast<char*>(blob.constData()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return std::nullopt;
    const QString value = QString::fromUtf8(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData));
    LocalFree(out.pbData);
    return value;
}

bool DpapiSecretStore::write(const QString& key, const QString& value) {
    const QByteArray utf8 = value.toUtf8();
    DATA_BLOB in{static_cast<DWORD>(utf8.size()), reinterpret_cast<BYTE*>(const_cast<char*>(utf8.constData()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"QAflow", nullptr, nullptr, nullptr, 0, &out)) return false;
    QSettings s;
    s.setValue(kGroup + QLatin1Char('/') + key, QByteArray(reinterpret_cast<const char*>(out.pbData), static_cast<int>(out.cbData)).toBase64());
    LocalFree(out.pbData);
    return true;
}

void DpapiSecretStore::remove(const QString& key) {
    QSettings s;
    s.remove(kGroup + QLatin1Char('/') + key);
}
#endif

// ---- PlainSettingsSecretStore ---------------------------------------------------------------

std::optional<QString> PlainSettingsSecretStore::read(const QString& key) {
    QSettings s;
    const QString path = QStringLiteral("secrets/") + key;
    if (!s.contains(path)) return std::nullopt;
    return s.value(path).toString();
}

bool PlainSettingsSecretStore::write(const QString& key, const QString& value) {
    QSettings s;
    s.setValue(QStringLiteral("secrets/") + key, value);
    return true;
}

void PlainSettingsSecretStore::remove(const QString& key) {
    QSettings s;
    s.remove(QStringLiteral("secrets/") + key);
}

QString PlainSettingsSecretStore::description() const {
    return QCoreApplication::translate("infrastructure", "Sin cifrar en %1").arg(QSettings().fileName());
}

// ---- Selección ------------------------------------------------------------------------------

std::shared_ptr<ISecretStore> makeSecretStore() {
    auto works = [](ISecretStore& store) {
        // Escritura de prueba: comprueba que el servicio de llavero responde y no sólo que el binario exista.
        const QString probe = QStringLiteral("qaflow/probe");
        const bool ok = store.write(probe, QStringLiteral("ok")) && store.read(probe).value_or(QString()) == QStringLiteral("ok");
        store.remove(probe);
        return ok;
    };
#ifdef Q_OS_WIN
    {
        auto dpapi = std::make_shared<DpapiSecretStore>();
        if (works(*dpapi)) return dpapi;
    }
#elif defined(Q_OS_MACOS)
    if (CommandSecretStore::available(CommandSecretStore::Tool::MacSecurity)) {
        auto mac = std::make_shared<CommandSecretStore>(CommandSecretStore::Tool::MacSecurity);
        if (works(*mac)) return mac;
    }
#else
    if (CommandSecretStore::available(CommandSecretStore::Tool::SecretTool)) {
        auto st = std::make_shared<CommandSecretStore>(CommandSecretStore::Tool::SecretTool);
        if (works(*st)) return st;
    }
#endif
    return std::make_shared<PlainSettingsSecretStore>();
}

} // namespace qaflow
