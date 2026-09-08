#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <functional>

namespace qaflow {

/// Captura a través de xdg-desktop-portal (org.freedesktop.portal.Screenshot): la única vía en
/// Wayland, donde QScreen::grabWindow devuelve negro. `interactive` deja que el compositor
/// muestre su propio selector (pantalla, ventana o región); sin él captura todo el escritorio.
///
/// Sólo existe con QtDBus (QAFLOW_HAS_DBUS); en otras plataformas `isAvailable()` es false.
class PortalScreenshot : public QObject {
    Q_OBJECT
public:
    using Done = std::function<void(const QImage& image, const QString& error)>;

    explicit PortalScreenshot(QObject* parent = nullptr);

    /// Hay un portal registrado en el bus de sesión que ofrece la interfaz Screenshot.
    static bool isAvailable();
    /// Sesión Wayland (grabWindow no sirve).
    static bool isWaylandSession();

    void take(bool interactive, Done done);
};

} // namespace qaflow
