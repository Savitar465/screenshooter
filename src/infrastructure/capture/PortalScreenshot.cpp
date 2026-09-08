#include "PortalScreenshot.h"

#include <QCoreApplication>
#include <QFile>
#include <QGuiApplication>
#include <QUrl>

#ifdef QAFLOW_HAS_DBUS
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>
#endif

namespace qaflow {


#ifdef QAFLOW_HAS_DBUS
namespace {

constexpr const char* kPortalService = "org.freedesktop.portal.Desktop";
constexpr const char* kPortalPath = "/org/freedesktop/portal/desktop";
constexpr const char* kScreenshotIface = "org.freedesktop.portal.Screenshot";
constexpr const char* kRequestIface = "org.freedesktop.portal.Request";

/// Espera la señal Response del objeto Request que el portal crea para cada llamada.
class RequestListener : public QObject {
    Q_OBJECT
public:
    RequestListener(PortalScreenshot::Done done, QObject* parent) : QObject(parent), m_done(std::move(done)) {}

    void fail(const QString& error) {
        if (m_done) { auto d = std::move(m_done); m_done = nullptr; d(QImage(), error); }
        deleteLater();
    }

public slots:
    void response(uint code, const QVariantMap& results) {
        if (code == 1) { fail(QCoreApplication::translate("infrastructure", "Captura cancelada")); return; }
        if (code != 0) { fail(QCoreApplication::translate("infrastructure", "El portal de capturas devolvió un error")); return; }
        const QString uri = results.value(QStringLiteral("uri")).toString();
        const QString path = QUrl(uri).toLocalFile();
        QImage img(path);
        if (img.isNull()) { fail(QCoreApplication::translate("infrastructure", "No se pudo leer la captura del portal (%1)").arg(uri)); return; }
        QFile::remove(path);   // el portal deja el fichero en la caché del usuario; ya no hace falta
        if (m_done) { auto d = std::move(m_done); m_done = nullptr; d(img, QString()); }
        deleteLater();
    }

private:
    PortalScreenshot::Done m_done;
};

} // namespace
#endif

PortalScreenshot::PortalScreenshot(QObject* parent) : QObject(parent) {}

bool PortalScreenshot::isWaylandSession() {
    return QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive)
        || qEnvironmentVariable("XDG_SESSION_TYPE").compare(QStringLiteral("wayland"), Qt::CaseInsensitive) == 0;
}

bool PortalScreenshot::isAvailable() {
#ifdef QAFLOW_HAS_DBUS
    static int cached = -1;
    if (cached >= 0) return cached == 1;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) { cached = 0; return false; }
    QDBusInterface iface(QLatin1String(kPortalService), QLatin1String(kPortalPath), QLatin1String(kScreenshotIface), bus);
    cached = iface.isValid() && iface.property("version").isValid() ? 1 : 0;
    return cached == 1;
#else
    return false;
#endif
}

void PortalScreenshot::take(bool interactive, Done done) {
#ifdef QAFLOW_HAS_DBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) { done(QImage(), QCoreApplication::translate("infrastructure", "No hay bus de sesión D-Bus")); return; }
    static int counter = 0;
    const QString token = QStringLiteral("qaflow_%1_%2").arg(QCoreApplication::applicationPid()).arg(++counter);
    // Ruta del objeto Request según la especificación: /request/<nombre único sin ':' y con '.'→'_'>/<token>.
    QString sender = bus.baseService().mid(1);
    sender.replace(QLatin1Char('.'), QLatin1Char('_'));
    const QString requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);

    auto* listener = new RequestListener(std::move(done), this);
    bus.connect(QLatin1String(kPortalService), requestPath, QLatin1String(kRequestIface), QStringLiteral("Response"),
                listener, SLOT(response(uint, QVariantMap)));

    QDBusMessage msg = QDBusMessage::createMethodCall(QLatin1String(kPortalService), QLatin1String(kPortalPath), QLatin1String(kScreenshotIface), QStringLiteral("Screenshot"));
    msg << QString() << QVariantMap{{QStringLiteral("handle_token"), token}, {QStringLiteral("interactive"), interactive}, {QStringLiteral("modal"), true}};
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg), listener);
    connect(watcher, &QDBusPendingCallWatcher::finished, listener, [listener, requestPath, bus](QDBusPendingCallWatcher* w) mutable {
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        w->deleteLater();
        if (reply.isError()) { listener->fail(QCoreApplication::translate("infrastructure", "El portal rechazó la captura: %1").arg(reply.error().message())); return; }
        // Portales antiguos devuelven otra ruta: escucharla también.
        const QString actual = reply.value().path();
        if (actual != requestPath)
            bus.connect(QLatin1String(kPortalService), actual, QLatin1String(kRequestIface), QStringLiteral("Response"), listener, SLOT(response(uint, QVariantMap)));
    });
#else
    Q_UNUSED(interactive);
    done(QImage(), QCoreApplication::translate("infrastructure", "Captura por portal no disponible en esta compilación"));
#endif
}

} // namespace qaflow

#ifdef QAFLOW_HAS_DBUS
#include "PortalScreenshot.moc"
#endif
