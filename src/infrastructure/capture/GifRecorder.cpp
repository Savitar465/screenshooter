#include "GifRecorder.h"

#include "infrastructure/capture/RecorderOverlay.h"
#include "infrastructure/capture/RegionSelector.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QFile>
#include <QScreen>

namespace qaflow {


GifRecorder::GifRecorder(QObject* parent) : QObject(parent) {
    connect(&m_timer, &QTimer::timeout, this, &GifRecorder::tick);
}

GifRecorder::~GifRecorder() {
    if (m_recording) finish(false, QCoreApplication::translate("infrastructure", "Grabación interrumpida"));
    if (m_overlay) m_overlay->close();
}

void GifRecorder::start(const RecordingOptions& options, Done done) {
    if (m_recording || m_starting) { if (done) done(RecordingResult{false, {}, 0, 0, QCoreApplication::translate("infrastructure", "Ya hay una grabación en curso")}); return; }
    if (QGuiApplication::platformName().contains(QStringLiteral("wayland"), Qt::CaseInsensitive)) {
        if (done) done(RecordingResult{false, {}, 0, 0, QCoreApplication::translate("infrastructure", "La grabación de GIF no está disponible en Wayland (usa una sesión X11)")});
        return;
    }
    m_options = options;
    m_done = std::move(done);
    m_starting = true;

    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QRect screenGeo = screen->geometry();

    // La ventana principal se oculta durante toda la grabación.
    m_hidAppWindow = m_appWindow && m_appWindow->isVisible();
    if (m_hidAppWindow) m_appWindow->hide();

    if (options.mode == CaptureMode::FullScreen) {
        QTimer::singleShot(m_hidAppWindow ? 250 : 0, this, [this, screenGeo]() { begin(QRect(QPoint(0, 0), screenGeo.size()), screenGeo); });
        return;
    }
    QTimer::singleShot(m_hidAppWindow ? 250 : 0, this, [this, screen, screenGeo]() {
        const QPixmap full = screen->grabWindow(0);
        if (full.isNull()) { m_starting = false; finish(false, QCoreApplication::translate("infrastructure", "No se pudo capturar la pantalla")); return; }
        auto* selector = new RegionSelector(full);
        selector->setGeometry(screenGeo);
        connect(selector, &RegionSelector::regionSelected, this, [this, screenGeo](const QRect& r) { begin(r, screenGeo); });
        connect(selector, &RegionSelector::cancelled, this, [this]() { m_starting = false; finish(false, QCoreApplication::translate("infrastructure", "Grabación cancelada")); });
        selector->showFullScreen();
        selector->activateWindow();
    });
}

void GifRecorder::begin(const QRect& region, const QRect& screenGeo) {
    m_starting = false;
    m_region = region;
    m_screenGeo = screenGeo;
    QScreen* screen = QGuiApplication::screenAt(screenGeo.center());
    const qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
    QSize frameSize = (QSizeF(region.size()) * dpr).toSize();
    if (frameSize.width() > m_options.maxWidth) frameSize = QSize(m_options.maxWidth, std::max(1, frameSize.height() * m_options.maxWidth / frameSize.width()));

    if (!m_encoder.open(m_options.outputPath, frameSize)) {
        finish(false, QCoreApplication::translate("infrastructure", "No se pudo crear %1: %2").arg(m_options.outputPath, m_encoder.error()));
        return;
    }
    m_overlay = new RecorderOverlay(screenGeo, region.translated(screenGeo.topLeft()));
    connect(m_overlay, &RecorderOverlay::stopRequested, this, &GifRecorder::stop);
    connect(m_overlay, &RecorderOverlay::cancelRequested, this, [this]() { finish(false, QCoreApplication::translate("infrastructure", "Grabación cancelada")); });
    m_overlay->setElapsed(0, m_options.maxSecs);
    m_overlay->show();

    m_recording = true;
    m_clock.start();
    m_lastFrameMs = 0;
    m_timer.start(std::max(50, 1000 / std::max(1, m_options.fps)));
    tick();   // primer fotograma inmediato
}

void GifRecorder::tick() {
    if (!m_recording) return;
    QScreen* screen = QGuiApplication::screenAt(m_screenGeo.center());
    if (!screen) screen = QGuiApplication::primaryScreen();
    const QPixmap pm = screen->grabWindow(0, m_region.x(), m_region.y(), m_region.width(), m_region.height());
    const qint64 now = m_clock.elapsed();
    // El retardo de cada fotograma es el tiempo real hasta el siguiente: si grabar tarda, el GIF sigue en sincronía.
    const int delay = static_cast<int>(now - m_lastFrameMs);
    if (!pm.isNull() && !m_encoder.addFrame(pm.toImage(), std::max(delay, 1000 / std::max(1, m_options.fps)))) {
        finish(false, QCoreApplication::translate("infrastructure", "No se pudo escribir la grabación: %1").arg(m_encoder.error()));
        return;
    }
    m_lastFrameMs = now;
    const int secs = static_cast<int>(now / 1000);
    if (m_overlay) m_overlay->setElapsed(secs, m_options.maxSecs);
    if (secs >= m_options.maxSecs) stop();
}

void GifRecorder::stop() {
    if (!m_recording) return;
    finish(true);
}

void GifRecorder::finish(bool keep, const QString& error) {
    m_timer.stop();
    const bool wasRecording = m_recording;
    m_recording = false;
    m_starting = false;
    RecordingResult r;
    r.path = m_options.outputPath;
    r.frames = m_encoder.frameCount();
    r.durationSecs = wasRecording ? m_clock.elapsed() / 1000.0 : 0;
    const bool written = wasRecording ? m_encoder.finish() : false;
    if (keep && written && r.frames > 0) {
        r.ok = true;
    } else {
        r.ok = false;
        r.error = error.isEmpty() ? (written ? QCoreApplication::translate("infrastructure", "La grabación no tiene fotogramas") : QCoreApplication::translate("infrastructure", "No se pudo escribir la grabación")) : error;
        QFile::remove(m_options.outputPath);
    }
    if (m_overlay) { m_overlay->close(); m_overlay = nullptr; }
    if (m_hidAppWindow && m_appWindow) { m_appWindow->show(); m_appWindow->raise(); m_appWindow->activateWindow(); }
    m_hidAppWindow = false;
    if (Done done = std::move(m_done)) { m_done = nullptr; done(r); }
}

} // namespace qaflow
