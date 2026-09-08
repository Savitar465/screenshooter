#pragma once

#include "core/models/Settings.h"

#include <QString>
#include <functional>

namespace qaflow {

struct RecordingOptions {
    CaptureMode mode = CaptureMode::FullScreen;   // FullScreen o Region (ActiveWindow se trata como Region)
    int fps = 10;
    int maxSecs = 30;
    int maxWidth = 1280;                          // los fotogramas se reducen si la pantalla es mayor
    QString outputPath;                           // fichero .gif de destino
};

struct RecordingResult {
    bool ok = false;
    QString path;
    int frames = 0;
    double durationSecs = 0;
    QString error;
};

/// Grabación de la pantalla a GIF. `start()` es asíncrono (puede pedir una región al usuario);
/// la grabación termina con `stop()`, al superar `maxSecs` o si el usuario la cancela desde el
/// control flotante. `done` se llama exactamente una vez por grabación iniciada.
class IScreenRecorder {
public:
    using Done = std::function<void(const RecordingResult&)>;
    virtual ~IScreenRecorder() = default;

    virtual void start(const RecordingOptions& options, Done done) = 0;
    virtual void stop() = 0;
    virtual bool isRecording() const = 0;
};

} // namespace qaflow
