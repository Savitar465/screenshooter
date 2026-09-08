#pragma once

#include "core/services/IScreenCapture.h"

#include <QObject>
#include <QPointer>
#include <QWidget>

namespace qaflow {

class PortalScreenshot;

/// Captura basada en QScreen::grabWindow (X11, Windows, macOS). Oculta la ventana principal
/// mientras captura para que la aplicación no salga en la evidencia.
///
/// En Wayland grabWindow devuelve negro: si hay xdg-desktop-portal se usa su interfaz Screenshot
/// (`PortalScreenshot`). «Pantalla completa» y «Región» piden una captura silenciosa del escritorio
/// (la región se recorta después con el mismo overlay); «Ventana activa» abre el selector
/// interactivo del compositor, que es la única forma de elegir una ventana en Wayland.
class ScreenCaptureService : public QObject, public IScreenCapture {
    Q_OBJECT
public:
    explicit ScreenCaptureService(QObject* parent = nullptr);

    /// Ventana que se oculta durante la captura (se fija cuando la UI ya existe).
    void setAppWindow(QWidget* w) { m_appWindow = w; }

    void capture(CaptureMode mode, Callback done) override;

    /// "QScreen::grabWindow" o "xdg-desktop-portal (Wayland)"; para mostrarlo en Ajustes.
    QString backendName() const;

private:
    void grabFullScreen(Callback done);
    void grabActiveWindow(Callback done);
    void grabRegion(Callback done);
    void captureViaPortal(CaptureMode mode, Callback done);
    void withWindowHidden(const std::function<void(std::function<void()> restore)>& body);
    static QPixmap grabScreenUnderCursor(QRect* screenGeometry = nullptr);
    static void selectRegion(const QPixmap& full, const QRect& screenGeo, std::function<void()> restore, Callback done);

    QPointer<QWidget> m_appWindow;
    PortalScreenshot* m_portal = nullptr;
    bool m_usePortal = false;
};

} // namespace qaflow
