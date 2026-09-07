#pragma once

#include "core/services/IScreenCapture.h"

#include <QObject>
#include <QPointer>
#include <QWidget>

namespace qaflow {

/// Captura basada en QScreen::grabWindow. Oculta la ventana principal mientras captura
/// para que la aplicación no salga en la evidencia.
class ScreenCaptureService : public QObject, public IScreenCapture {
    Q_OBJECT
public:
    explicit ScreenCaptureService(QObject* parent = nullptr);

    /// Ventana que se oculta durante la captura (se fija cuando la UI ya existe).
    void setAppWindow(QWidget* w) { m_appWindow = w; }

    void capture(CaptureMode mode, Callback done) override;

private:
    void grabFullScreen(Callback done);
    void grabActiveWindow(Callback done);
    void grabRegion(Callback done);
    void withWindowHidden(const std::function<void(std::function<void()> restore)>& body);
    static QPixmap grabScreenUnderCursor(QRect* screenGeometry = nullptr);

    QPointer<QWidget> m_appWindow;
};

} // namespace qaflow
