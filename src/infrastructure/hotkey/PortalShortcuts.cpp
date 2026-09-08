#include "PortalShortcuts.h"

#include <QCoreApplication>
#include <QKeySequence>

#ifdef QAFLOW_HAS_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCall>

namespace qaflow {

/// Elemento de a(sa{sv}): (id, {description, preferred_trigger}).
struct PortalShortcutEntry {
    QString id;
    QVariantMap props;
};
using PortalShortcutList = QList<PortalShortcutEntry>;

QDBusArgument& operator<<(QDBusArgument& a, const PortalShortcutEntry& s) {
    a.beginStructure();
    a << s.id << s.props;
    a.endStructure();
    return a;
}
const QDBusArgument& operator>>(const QDBusArgument& a, PortalShortcutEntry& s) {
    a.beginStructure();
    a >> s.id >> s.props;
    a.endStructure();
    return a;
}

namespace {

constexpr const char* kService = "org.freedesktop.portal.Desktop";
constexpr const char* kPath = "/org/freedesktop/portal/desktop";
constexpr const char* kIface = "org.freedesktop.portal.GlobalShortcuts";
constexpr const char* kRequestIface = "org.freedesktop.portal.Request";

/// "Ctrl+Shift+S" → "CTRL+SHIFT+s" (formato de la especificación de atajos de XDG).
QString toPortalTrigger(const QString& sequence) {
    const QKeySequence seq(sequence.trimmed(), QKeySequence::PortableText);
    if (seq.isEmpty()) return {};
    const QKeyCombination kc = seq[0];
    QStringList parts;
    if (kc.keyboardModifiers() & Qt::ControlModifier) parts << QStringLiteral("CTRL");
    if (kc.keyboardModifiers() & Qt::AltModifier) parts << QStringLiteral("ALT");
    if (kc.keyboardModifiers() & Qt::ShiftModifier) parts << QStringLiteral("SHIFT");
    if (kc.keyboardModifiers() & Qt::MetaModifier) parts << QStringLiteral("LOGO");
    QString key = QKeySequence(kc.key()).toString(QKeySequence::PortableText);
    if (key.size() == 1) key = key.toLower();
    parts << key;
    return parts.join(QLatin1Char('+'));
}

} // namespace
} // namespace qaflow

Q_DECLARE_METATYPE(qaflow::PortalShortcutEntry)
Q_DECLARE_METATYPE(qaflow::PortalShortcutList)

namespace qaflow {

PortalShortcuts::PortalShortcuts(QObject* parent) : QObject(parent) {
    qDBusRegisterMetaType<PortalShortcutEntry>();
    qDBusRegisterMetaType<PortalShortcutList>();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    QDBusInterface iface(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface), bus);
    m_available = iface.isValid() && iface.property("version").isValid();
    if (!m_available) return;
    m_sender = bus.baseService().mid(1);
    m_sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    bus.connect(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface), QStringLiteral("Activated"),
                this, SLOT(activated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
    m_rebindTimer.setSingleShot(true);
    m_rebindTimer.setInterval(0);
    connect(&m_rebindTimer, &QTimer::timeout, this, &PortalShortcuts::rebind);
}

PortalShortcuts::~PortalShortcuts() { closeSession(); }

QString PortalShortcuts::requestPath(const QString& token) const {
    return QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(m_sender, token);
}

bool PortalShortcuts::bind(const QString& id, const QString& sequence, std::function<void()> onActivated) {
    if (!m_available) return false;
    unbind(id);
    m_entries.append(Entry{id, sequence, std::move(onActivated)});
    m_rebindTimer.start();
    return true;
}

void PortalShortcuts::unbind(const QString& id) {
    for (int i = m_entries.size() - 1; i >= 0; --i) if (m_entries[i].id == id) m_entries.removeAt(i);
    if (m_available) m_rebindTimer.start();
}

void PortalShortcuts::closeSession() {
    if (m_session.isEmpty()) return;
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), m_session, QStringLiteral("org.freedesktop.portal.Session"), QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(msg);
    m_session.clear();
}

void PortalShortcuts::rebind() {
    closeSession();
    if (m_entries.isEmpty()) return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = QStringLiteral("qaflow_req_%1").arg(++m_counter);
    const QString sessionToken = QStringLiteral("qaflow_ses_%1").arg(m_counter);
    bus.connect(QLatin1String(kService), requestPath(token), QLatin1String(kRequestIface), QStringLiteral("Response"), this, SLOT(sessionResponse(uint, QVariantMap)));
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface), QStringLiteral("CreateSession"));
    msg << QVariantMap{{QStringLiteral("handle_token"), token}, {QStringLiteral("session_handle_token"), sessionToken}};
    bus.asyncCall(msg);
}

void PortalShortcuts::sessionResponse(uint code, const QVariantMap& results) {
    if (code != 0) return;
    m_session = results.value(QStringLiteral("session_handle")).toString();
    if (m_session.isEmpty()) return;
    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = QStringLiteral("qaflow_bind_%1").arg(++m_counter);
    bus.connect(QLatin1String(kService), requestPath(token), QLatin1String(kRequestIface), QStringLiteral("Response"), this, SLOT(bindResponse(uint, QVariantMap)));
    PortalShortcutList list;
    for (const auto& e : m_entries) {
        PortalShortcutEntry s;
        s.id = e.id;
        s.props[QStringLiteral("description")] = QCoreApplication::translate("infrastructure", "QAflow · %1").arg(e.id);
        const QString trigger = toPortalTrigger(e.sequence);
        if (!trigger.isEmpty()) s.props[QStringLiteral("preferred_trigger")] = trigger;
        list << s;
    }
    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kService), QLatin1String(kPath), QLatin1String(kIface), QStringLiteral("BindShortcuts"));
    msg << QVariant::fromValue(QDBusObjectPath(m_session)) << QVariant::fromValue(list) << QString()
        << QVariantMap{{QStringLiteral("handle_token"), token}};
    bus.asyncCall(msg);
}

void PortalShortcuts::bindResponse(uint code, const QVariantMap& results) {
    Q_UNUSED(results);
    if (code != 0) closeSession();   // el usuario canceló el diálogo del compositor
}

void PortalShortcuts::activated(const QDBusObjectPath& session, const QString& id, qulonglong timestamp, const QVariantMap& options) {
    Q_UNUSED(timestamp); Q_UNUSED(options);
    if (session.path() != m_session) return;
    for (const auto& e : m_entries) if (e.id == id && e.callback) { e.callback(); return; }
}

} // namespace qaflow

#else   // sin QtDBus: la clase existe pero nunca está disponible

namespace qaflow {
PortalShortcuts::PortalShortcuts(QObject* parent) : QObject(parent) {}
PortalShortcuts::~PortalShortcuts() = default;
bool PortalShortcuts::bind(const QString&, const QString&, std::function<void()>) { return false; }
void PortalShortcuts::unbind(const QString&) {}
void PortalShortcuts::rebind() {}
void PortalShortcuts::closeSession() {}
QString PortalShortcuts::requestPath(const QString& token) const { return token; }
} // namespace qaflow

#endif
