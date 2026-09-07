#include "EvidenceService.h"

#include "application/RunController.h"
#include "application/SettingsStore.h"
#include "application/TestCaseStore.h"

#include <QDir>
#include <QFileInfo>

namespace qaflow {

EvidenceService::EvidenceService(std::shared_ptr<IScreenCapture> capture, TestCaseStore& cases, RunController& run,
                                 SettingsStore& settings, QObject* parent)
    : QObject(parent), m_capture(std::move(capture)), m_cases(cases), m_run(run), m_settings(settings) {}

void EvidenceService::captureForSelectedCase() {
    if (m_busy || !m_capture) return;
    const QString caseId = m_cases.selectedId();
    if (caseId.isEmpty()) { emit failed(QStringLiteral("No hay ningún caso seleccionado")); return; }

    m_busy = true;
    const CaptureSettings cfg = m_settings.capture();
    // Si la ejecución activa es la del caso seleccionado, la captura se asocia al paso actual.
    const int step = (m_run.isRunning() && m_run.state().caseId == caseId) ? m_run.state().idx + 1 : 0;

    m_capture->capture(cfg.mode, [this, caseId, cfg, step](const CaptureResult& r) {
        m_busy = false;
        if (!r.ok) { emit failed(r.error); return; }

        QDir dir(cfg.folder);
        if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
            emit failed(QStringLiteral("No se pudo crear la carpeta %1").arg(cfg.folder));
            return;
        }
        const int seq = m_cases.nextShotSequence();
        const QString name = QStringLiteral("cap_%1.%2").arg(seq, 3, 10, QLatin1Char('0')).arg(cfg.extension());
        const QString path = dir.filePath(name);
        const char* fmt = cfg.extension() == QStringLiteral("jpg") ? "JPG" : cfg.extension() == QStringLiteral("webp") ? "WEBP" : "PNG";
        if (!r.image.save(path, fmt)) {
            emit failed(QStringLiteral("No se pudo guardar %1").arg(path));
            return;
        }
        m_cases.addShot(caseId, Screenshot{seq, step, name, path});
        emit captured(path);
    });
}

} // namespace qaflow
