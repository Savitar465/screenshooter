#pragma once

#include <QList>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <functional>

#ifdef QAFLOW_HAS_DBUS
#include <QDBusObjectPath>
#endif

namespace qaflow {

/// Atajos globales en Wayland mediante org.freedesktop.portal.GlobalShortcuts. El portal crea
/// una sesión, se le piden los atajos con su combinación preferida (el compositor puede mostrar
/// un diálogo para que el usuario los confirme) y avisa con la señal Activated.
/// Cada cambio en el conjunto de atajos cierra la sesión y abre otra: BindShortcuts es de un solo uso.
class PortalShortcuts : public QObject {
    Q_OBJECT
public:
    explicit PortalShortcuts(QObject* parent = nullptr);
    ~PortalShortcuts() override;

    bool isAvailable() const { return m_available; }
    bool bind(const QString& id, const QString& sequence, std::function<void()> onActivated);
    void unbind(const QString& id);

private slots:
#ifdef QAFLOW_HAS_DBUS
    void sessionResponse(uint code, const QVariantMap& results);
    void bindResponse(uint code, const QVariantMap& results);
    void activated(const QDBusObjectPath& session, const QString& id, qulonglong timestamp, const QVariantMap& options);
#endif

private:
    struct Entry {
        QString id;
        QString sequence;
        std::function<void()> callback;
    };
    void rebind();
    void closeSession();
    QString requestPath(const QString& token) const;

    QList<Entry> m_entries;
    QTimer m_rebindTimer;
    bool m_available = false;
    QString m_session;      // ruta del objeto Session activo
    QString m_sender;       // nombre único del bus, normalizado para las rutas de Request
    int m_counter = 0;
};

} // namespace qaflow
