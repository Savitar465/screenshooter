#include "EvidenceService.h"

#include "application/RunController.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"

#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>

namespace qaflow {

EvidenceService::EvidenceService(std::shared_ptr<IScreenCapture> capture, TestCaseStore& cases, RunController& run,
                                 SettingsStore& settings, QObject* parent)
    : QObject(parent), m_capture(std::move(capture)), m_cases(cases), m_run(run), m_settings(settings) {
    // Cuando una captura deja de estar referenciada (y ya no se puede deshacer), su fichero se borra.
    connect(&m_cases, &TestCaseStore::filesReleased, this, [](const QStringList& paths) {
        for (const auto& p : paths) if (QFileInfo::exists(p)) QFile::remove(p);
    });
    m_countdown.setInterval(1000);
    connect(&m_countdown, &QTimer::timeout, this, [this]() {
        if (--m_countdownLeft > 0) { emit countdown(m_countdownLeft); return; }
        m_countdown.stop();
        m_countdownLeft = 0;
        emit countdown(0);
        grabNow();
    });
}

void EvidenceService::setRecorder(std::shared_ptr<IScreenRecorder> recorder) { m_recorder = std::move(recorder); }

QString EvidenceService::targetCaseId() const { return m_cases.selectedId(); }

/// Si la ejecución activa es la del caso, la evidencia se asocia al paso actual.
int EvidenceService::targetStep(const QString& caseId) const {
    return (m_run.isRunning() && m_run.state().caseId == caseId) ? m_run.state().idx + 1 : 0;
}

bool EvidenceService::ensureFolder(QString* error) const {
    const QString folder = m_settings.capture().folder;
    QDir dir(folder);
    if (dir.exists() || dir.mkpath(QStringLiteral("."))) return true;
    if (error) *error = tr("No se pudo crear la carpeta %1").arg(folder);
    return false;
}

// ---- Captura ---------------------------------------------------------------------------------

void EvidenceService::captureForSelectedCase() {
    if (m_countdownLeft > 0) {   // segunda pulsación durante la cuenta atrás: cancelar
        m_countdown.stop();
        m_countdownLeft = 0;
        emit countdown(0);
        emit failed(tr("Cuenta atrás cancelada"));
        return;
    }
    if (m_busy || !m_capture || isRecording()) return;
    if (targetCaseId().isEmpty()) { emit failed(tr("No hay ningún caso seleccionado")); return; }
    const int delay = m_settings.capture().delaySecs;
    if (delay <= 0) { grabNow(); return; }
    m_countdownLeft = delay;
    emit countdown(m_countdownLeft);
    m_countdown.start();
}

void EvidenceService::grabNow() {
    const QString caseId = targetCaseId();
    if (caseId.isEmpty()) { emit failed(tr("No hay ningún caso seleccionado")); return; }
    m_busy = true;
    const CaptureSettings cfg = m_settings.capture();
    m_capture->capture(cfg.mode, [this, caseId, cfg](const CaptureResult& r) {
        m_busy = false;
        if (!r.ok) { emit failed(r.error); return; }
        QString error;
        if (!ensureFolder(&error)) { emit failed(error); return; }
        // El nombre lleva el número de secuencia que tendrá la evidencia: se reserva al guardar.
        const int seq = m_cases.nextShotSequence();
        const QString name = QStringLiteral("cap_%1.%2").arg(seq, 3, 10, QLatin1Char('0')).arg(cfg.extension());
        const QString path = QDir(cfg.folder).filePath(name);
        const char* fmt = cfg.extension() == QStringLiteral("jpg") ? "JPG" : cfg.extension() == QStringLiteral("webp") ? "WEBP" : "PNG";
        if (!r.image.save(path, fmt)) { emit failed(tr("No se pudo guardar %1").arg(path)); return; }
        m_cases.addShot(caseId, Screenshot{seq, targetStep(caseId), name, path});
        if (cfg.copyToClipboard) if (QClipboard* cb = QGuiApplication::clipboard()) cb->setImage(r.image);
        emit shotAdded(caseId, seq, path);
        emit captured(path);
    });
}

// ---- Grabación -------------------------------------------------------------------------------

bool EvidenceService::isRecording() const { return m_recorder && m_recorder->isRecording(); }

void EvidenceService::toggleRecording() {
    if (!m_recorder) { emit failed(tr("La grabación no está disponible")); return; }
    if (m_recorder->isRecording()) { m_recorder->stop(); return; }
    if (m_busy || m_countdownLeft > 0) return;
    const QString caseId = targetCaseId();
    if (caseId.isEmpty()) { emit failed(tr("No hay ningún caso seleccionado")); return; }
    QString error;
    if (!ensureFolder(&error)) { emit failed(error); return; }
    const CaptureSettings cfg = m_settings.capture();
    RecordingOptions opts;
    opts.mode = cfg.mode == CaptureMode::FullScreen ? CaptureMode::FullScreen : CaptureMode::Region;
    opts.fps = cfg.gifFps;
    opts.maxSecs = cfg.gifMaxSecs;
    const int seq = m_cases.nextShotSequence();
    const QString name = QStringLiteral("rec_%1.gif").arg(seq, 3, 10, QLatin1Char('0'));
    opts.outputPath = QDir(cfg.folder).filePath(name);
    m_busy = true;
    m_recorder->start(opts, [this, caseId, seq, name](const RecordingResult& r) {
        m_busy = false;
        emit recordingChanged(false);
        if (!r.ok) { emit failed(r.error); return; }
        m_cases.addShot(caseId, Screenshot{seq, targetStep(caseId), name, r.path});
        emit shotAdded(caseId, seq, r.path);
        emit captured(r.path);
    });
    // `start` puede fallar de forma síncrona (Wayland, otra grabación): sólo avisamos si sigue viva.
    if (m_recorder->isRecording() || m_busy) emit recordingChanged(true);
}

// ---- Adjuntos, portapapeles y edición --------------------------------------------------------

int EvidenceService::attachFiles(const QStringList& paths) {
    const QString caseId = targetCaseId();
    if (caseId.isEmpty()) { emit failed(tr("No hay ningún caso seleccionado")); return 0; }
    QString error;
    if (!ensureFolder(&error)) { emit failed(error); return 0; }
    const QDir dir(m_settings.capture().folder);
    QStringList added;
    for (const QString& src : paths) {
        const QFileInfo info(src);
        if (!info.isFile()) { emit failed(tr("No existe %1").arg(src)); continue; }
        const int seq = m_cases.nextShotSequence();
        const QString name = QStringLiteral("adj_%1_%2").arg(seq, 3, 10, QLatin1Char('0')).arg(info.fileName());
        const QString dst = dir.filePath(name);
        if (!QFile::copy(src, dst)) { emit failed(tr("No se pudo copiar %1 a la carpeta de capturas").arg(info.fileName())); continue; }
        QFile::setPermissions(dst, QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
        m_cases.addShot(caseId, Screenshot{seq, targetStep(caseId), name, dst});
        emit shotAdded(caseId, seq, dst);
        added << dst;
    }
    if (!added.isEmpty()) emit attached(added);
    return added.size();
}

bool EvidenceService::replaceImage(const QString& caseId, int shotId, const QImage& image) {
    const TestCase* c = m_cases.find(caseId);
    if (!c || image.isNull()) return false;
    for (const auto& s : c->shots) {
        if (s.id != shotId) continue;
        const QString ext = s.extension();
        const char* fmt = ext == QStringLiteral("jpg") || ext == QStringLiteral("jpeg") ? "JPG" : ext == QStringLiteral("webp") ? "WEBP" : "PNG";
        if (!image.save(s.path, fmt)) { emit failed(tr("No se pudo guardar %1").arg(s.path)); return false; }
        m_cases.notifyShotFileChanged(caseId);
        return true;
    }
    return false;
}

bool EvidenceService::copyToClipboard(const QString& path) {
    const QImage img(path);
    QClipboard* cb = QGuiApplication::clipboard();
    if (img.isNull() || !cb) { emit failed(tr("No se pudo copiar la imagen al portapapeles")); return false; }
    cb->setImage(img);
    return true;
}

} // namespace qaflow
