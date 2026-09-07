#pragma once

#include "core/models/Settings.h"

#include <QImage>
#include <QString>
#include <functional>

namespace qaflow {

struct CaptureResult {
    bool ok = false;
    QImage image;
    QString error;
};

/// Captura de pantalla. Asíncrona porque algunos modos (región) requieren interacción del usuario.
class IScreenCapture {
public:
    using Callback = std::function<void(const CaptureResult&)>;
    virtual ~IScreenCapture() = default;
    virtual void capture(CaptureMode mode, Callback done) = 0;
};

} // namespace qaflow
