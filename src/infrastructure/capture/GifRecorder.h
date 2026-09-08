#pragma once

#include "core/services/IScreenRecorder.h"
#include "infrastructure/capture/GifEncoder.h"

#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QRect>
#include <QTimer>
#include <QWidget>

namespace qaflow {

class RecorderOverlay;

/// Grabación a GIF con QScreen::grabWindow a intervalos fijos. Región: el usuario la elige con
/// RegionSelector; pantalla completa: la pantalla bajo el cursor. La ventana principal se oculta
/// durante la grabación (como en las capturas) y vuelve al terminar.
///
/// En Wayland grabWindow devuelve negro: `start()` falla con un mensaje claro.
class GifRecorder : public QObject, public IScreenRecorder {
    Q_OBJECT
public:
    explicit GifRecorder(QObject* parent = nullptr);
    ~GifRecorder() override;

    void setAppWindow(QWidget* w) { m_appWindow = w; }

    void start(const RecordingOptions& options, Done done) override;
    void stop() override;
    bool isRecording() const override { return m_recording; }

private:
    void begin(const QRect& region, const QRect& screenGeo);
    void tick();
    void finish(bool keep, const QString& error = {});

    QPointer<QWidget> m_appWindow;
    QPointer<RecorderOverlay> m_overlay;
    RecordingOptions m_options;
    Done m_done;
    GifEncoder m_encoder;
    QTimer m_timer;
    QElapsedTimer m_clock;
    qint64 m_lastFrameMs = 0;
    QRect m_region;      // en coordenadas lógicas de la pantalla
    QRect m_screenGeo;
    bool m_recording = false;
    bool m_starting = false;
    bool m_hidAppWindow = false;
};

} // namespace qaflow
