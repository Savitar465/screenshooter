#pragma once

#include "core/services/IScreenCapture.h"
#include "infrastructure/capture/ScreenPicker.h"

#include <QObject>
#include <QPointer>
#include <QWidget>

namespace qaflow {

class PortalScreenshot;

/// Captura basada en QScreen::grabWindow (X11, Windows, macOS). La pantalla sale de `ScreenPicker`
/// (bajo el cursor, la que no tiene QAflow o una fija). Si la ventana principal está en esa pantalla
/// se oculta mientras captura para que no salga en la evidencia; si está en otra, no se toca.
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
    /// Pantalla a capturar (`CaptureSettings::screen` / `screenName`).
    void setScreenTarget(CaptureScreen target, const QString& screenName) { m_picker.setTarget(target, screenName); }

    void capture(CaptureMode mode, Callback done) override;

    /// "QScreen::grabWindow" o "xdg-desktop-portal (Wayland)"; para mostrarlo en Ajustes.
    QString backendName() const;

private:
    void grabFullScreen(QScreen* screen, Callback done);
    void grabActiveWindow(QScreen* screen, Callback done);
    void grabRegion(QScreen* screen, Callback done);
    void captureViaPortal(CaptureMode mode, QScreen* screen, Callback done);
    QWidget* appWindow() const;
    /// Oculta la ventana si está en `screen`, espera a que el compositor repinte y ejecuta `body`,
    /// que recibe la pantalla (primaria si se desconectó entretanto) y `restore` para volver a mostrarla.
    void withWindowHidden(QScreen* screen, const std::function<void(QScreen* screen, std::function<void()> restore)>& body);
    static void selectRegion(const QPixmap& full, const QRect& screenGeo, std::function<void()> restore, Callback done);

    QPointer<QWidget> m_appWindow;
    ScreenPicker m_picker;
    PortalScreenshot* m_portal = nullptr;
    bool m_usePortal = false;
};

} // namespace qaflow
