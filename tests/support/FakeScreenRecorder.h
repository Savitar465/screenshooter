#pragma once

// IScreenRecorder falso: no graba nada. `start()` deja la grabación "en curso" y `stop()` escribe
// un fichero mínimo en `outputPath` y llama al callback con éxito. `failOnStart` simula una
// plataforma sin soporte (Wayland).

#include "core/services/IScreenRecorder.h"

#include <QFile>

namespace qaflow::testing {

class FakeScreenRecorder : public IScreenRecorder {
public:
    bool failOnStart = false;
    int starts = 0;
    RecordingOptions lastOptions;

    void start(const RecordingOptions& options, Done done) override {
        ++starts;
        lastOptions = options;
        if (failOnStart) { done(RecordingResult{false, {}, 0, 0, QStringLiteral("sin soporte")}); return; }
        m_done = std::move(done);
        m_recording = true;
    }

    void stop() override {
        if (!m_recording) return;
        m_recording = false;
        QFile f(lastOptions.outputPath);
        f.open(QIODevice::WriteOnly);
        f.write("GIF89a");
        f.close();
        RecordingResult r;
        r.ok = true;
        r.path = lastOptions.outputPath;
        r.frames = 3;
        r.durationSecs = 0.3;
        if (Done d = std::move(m_done)) d(r);
    }

    bool isRecording() const override { return m_recording; }

private:
    Done m_done;
    bool m_recording = false;
};

} // namespace qaflow::testing
