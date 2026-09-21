#pragma once

#include "core/models/Settings.h"

#include <QString>

class QScreen;
class QWidget;

namespace qaflow {

/// Elige la pantalla que se captura o se graba según `CaptureSettings::screen`. Con un solo
/// monitor todas las opciones dan la misma pantalla.
class ScreenPicker {
public:
    void setTarget(CaptureScreen target, const QString& screenName) { m_target = target; m_screenName = screenName; }

    /// Pantalla a capturar; `appWindow` es la ventana de QAflow (puede ser nula u oculta).
    QScreen* pick(const QWidget* appWindow) const;

    /// La ventana principal visible de QAflow, si no se fijó una explícitamente.
    static QWidget* findAppWindow();
    /// La ventana está visible y ocupa parte de `screen`: saldría en la captura.
    static bool isOnScreen(const QWidget* window, const QScreen* screen);

private:
    CaptureScreen m_target = CaptureScreen::UnderCursor;
    QString m_screenName;
};

} // namespace qaflow
