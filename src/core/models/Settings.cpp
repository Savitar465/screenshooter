#include "Settings.h"

namespace qaflow {

QString toString(CaptureMode m) {
    switch (m) {
        case CaptureMode::FullScreen: return QStringLiteral("Pantalla completa");
        case CaptureMode::ActiveWindow: return QStringLiteral("Ventana activa");
        case CaptureMode::Region: return QStringLiteral("Región");
    }
    return {};
}

CaptureMode captureModeFromString(const QString& s) {
    if (s == QStringLiteral("Pantalla completa")) return CaptureMode::FullScreen;
    if (s == QStringLiteral("Región")) return CaptureMode::Region;
    return CaptureMode::ActiveWindow;
}

} // namespace qaflow
